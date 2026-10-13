#include <pebble.h>

#define TOTAL_SEGMENTOS 6
#define DURACION_NORMAL_S 30
#define DURACION_RAPIDO_S 15
#define CUENTA_REGRESIVA_S 3
#define MI_ACTION_BAR_WIDTH 30
#define CONFIRMAR_DETENER_MS 2500

#define KEY_BG_INDEX 100
#define KEY_FG_INDEX 101
#define KEY_CICLO_ACTIVO 102
#define KEY_INICIO_EPOCH 103
#define KEY_PAUSADO 104
#define KEY_PAUSA_INICIO_EPOCH 105
#define KEY_ACUM_PAUSADO 106
#define KEY_DURACION_SEGMENTO 107
#define KEY_INICIO_MS 108
#define KEY_PAUSA_INICIO_MS 109

// ===================== Textos (español / inglés) =====================
typedef struct {
  const char *titulo, *por_lado_normal, *por_lado_rapido, *preparate, *toca;
  const char *pausado, *tramo, *listo, *otra_vez;
  const char *normal, *rapido, *color_fondo, *color_letras;
  const char *colores[10];
} Textos;

static const Textos ES = {
  "Cepillado", "30s por lado", "15s por lado", "Prepárate...", "Toca o SELECT",
  "Pausado (%d de %d)", "Tramo %d de %d", "¡Listo, cepillado!", "Atrás otra vez: detener",
  "NORMAL", "RÁPIDO", "Color de fondo", "Color de letras",
  { "Blanco", "Negro", "Azul", "Verde", "Rojo", "Amarillo", "Naranja", "Morado", "Magenta", "Gris claro" },
};

static const Textos EN = {
  "Brushing", "30s per side", "15s per side", "Get ready...", "Tap or SELECT",
  "Paused (%d of %d)", "Part %d of %d", "Done, all clean!", "Back again: stop",
  "NORMAL", "QUICK", "Background color", "Text color",
  { "White", "Black", "Blue", "Green", "Red", "Yellow", "Orange", "Purple", "Magenta", "Light gray" },
};

static const Textos *T = &EN;

#if defined(PBL_COLOR)
  #define NUM_COLORS 10
  static const GColor PALETTE[NUM_COLORS] = {
    GColorWhite, GColorBlack, GColorBlue, GColorGreen,
    GColorRed, GColorYellow, GColorOrange, GColorPurple,
    GColorMagenta, GColorLightGray
  };
#else
  #define NUM_COLORS 2
  static const GColor PALETTE[NUM_COLORS] = { GColorWhite, GColorBlack };
#endif

// Fredoka (solo dígitos), lo más grande que admite cada plataforma: el
// conversor de fuentes limita cada glifo a 512 bytes (256 en las pantallas pequeñas).
#if PBL_DISPLAY_WIDTH >= 200
  #define FUENTE_NUMEROS RESOURCE_ID_FREDOKA_GRANDE_96
#else
  #define FUENTE_NUMEROS RESOURCE_ID_FREDOKA_CHICA_72
#endif

static int s_bg_index = 0;
static int s_fg_index = 1;
static GColor s_bg_color;
static GColor s_fg_color;
static GFont s_fuente_numeros;

static Window *s_window;
static Layer *s_canvas_layer;
static Layer *s_action_bar_layer;
// El número se pinta dos veces, cada capa recortada a un lado de la cortina,
// para que cambie de color justo donde la cortina lo cruza.
static Layer *s_numero_sobre;   // parte ya revelada (arriba)
static Layer *s_numero_bajo;    // parte aún cubierta por la cortina
static GRect s_area_numero;     // en coordenadas del lienzo
static int s_frontera_y;        // altura de la cortina, calculada al refrescar

static AppTimer *s_tick_timer;
static AppTimer *s_countdown_timer;
static AppTimer *s_confirmar_timer;

static int s_segmento_actual = 0;
static int s_segundos_restantes_seg = 0;
static bool s_ciclo_activo = false;
static bool s_pausado = false;
static bool s_completado = false;
static bool s_confirmando_detener = false;

static bool s_cuenta_regresiva_activa = false;
static int s_cuenta_regresiva_valor = 0;

static bool s_eligiendo_modo = false;
static int s_modo = 0; // 0 = normal (30s), 1 = rápido (15s)
static int s_duracion_segmento = DURACION_NORMAL_S;

// Tiempos en milisegundos desde 1970: el avance se calcula siempre desde aquí,
// así el contador no se desfasa aunque un tick llegue tarde.
static int64_t s_inicio_ms = 0;
static int64_t s_pausa_inicio_ms = 0;
static int32_t s_acumulado_pausado_ms = 0;

static char s_status_buf[40];
static char s_numero_buf[4];

static Window *s_settings_window;
static MenuLayer *s_menu_layer;

static int64_t ahora_ms(void) {
  time_t s;
  uint16_t ms;
  time_ms(&s, &ms);
  return (int64_t)s * 1000 + ms;
}

static int32_t transcurridos_ms(void) {
#ifdef CAPTURAS
  return 78000;   // tramo 3 con 12 s y la cortina a media pantalla (herramientas/capturas.sh)
#endif
  int64_t hasta = s_pausado ? s_pausa_inicio_ms : ahora_ms();
  int64_t t = hasta - s_inicio_ms - s_acumulado_pausado_ms;
  return t < 0 ? 0 : (int32_t)t;
}

static int32_t total_ms(void) { return TOTAL_SEGMENTOS * s_duracion_segmento * 1000; }

static int frontera_cortina(GRect bounds);
static void colocar_capas_numero(int frontera_y);

// ===================== Tema de colores =====================
// La cortina se calcula una sola vez por refresco: el lienzo y las dos capas del
// número usan el mismo valor, así nunca quedan desalineados.
static void refrescar_lienzo(void) {
  if (!s_canvas_layer) return;
  s_frontera_y = frontera_cortina(layer_get_bounds(s_canvas_layer));
  colocar_capas_numero(s_frontera_y);
  layer_mark_dirty(s_canvas_layer);
}
static void refrescar_action_bar(void) { if (s_action_bar_layer) layer_mark_dirty(s_action_bar_layer); }

static void apply_theme(void) {
  s_bg_color = PALETTE[s_bg_index];
  s_fg_color = PALETTE[s_fg_index];
  if (s_window) window_set_background_color(s_window, s_bg_color);
  refrescar_lienzo();
  refrescar_action_bar();
}

// ===================== Texto de estatus =====================
static void actualizar_status(void) {
  if (s_confirmando_detener) {
    snprintf(s_status_buf, sizeof(s_status_buf), "%s", T->otra_vez);
  } else if (s_eligiendo_modo) {
    snprintf(s_status_buf, sizeof(s_status_buf), "%s", s_modo == 0 ? T->por_lado_normal : T->por_lado_rapido);
  } else if (s_cuenta_regresiva_activa) {
    snprintf(s_status_buf, sizeof(s_status_buf), "%s", T->preparate);
  } else if (!s_ciclo_activo && s_segmento_actual == 0 && !s_completado) {
    snprintf(s_status_buf, sizeof(s_status_buf), "%s", T->toca);
  } else if (s_ciclo_activo && s_pausado) {
    snprintf(s_status_buf, sizeof(s_status_buf), T->pausado, s_segmento_actual, TOTAL_SEGMENTOS);
  } else if (s_ciclo_activo) {
    snprintf(s_status_buf, sizeof(s_status_buf), T->tramo, s_segmento_actual, TOTAL_SEGMENTOS);
  } else {
    snprintf(s_status_buf, sizeof(s_status_buf), "%s", T->listo);
  }
}

// ===================== Lienzo principal (cortina + texto + número) =====================
static GColor color_por_y(int y_centro, int frontera_y, GColor sobre, GColor bajo) {
  return (y_centro < frontera_y) ? sobre : bajo;
}

static int frontera_cortina(GRect bounds) {
  int32_t total = total_ms();
  int32_t t = 0;
  if (s_completado) t = total;
  else if (s_ciclo_activo) t = transcurridos_ms();
  if (t > total) t = total;
  return (int)(((int64_t)bounds.size.h * t) / total);
}

// Qué número mostrar, o NULL si no toca ninguno.
static const char *texto_numero(void) {
  int valor;
  if (s_eligiendo_modo) return NULL;
  if (s_cuenta_regresiva_activa) valor = s_cuenta_regresiva_valor;
  else if (s_ciclo_activo) valor = s_segundos_restantes_seg;
  else if (s_completado) valor = 0;
  else return NULL;
  snprintf(s_numero_buf, sizeof(s_numero_buf), "%d", valor);
  return s_numero_buf;
}

// Coloca las dos capas del número a cada lado de la cortina.
static void colocar_capas_numero(int frontera_y) {
  if (!s_numero_sobre) return;
  int arriba = s_area_numero.origin.y, abajo = arriba + s_area_numero.size.h;
  int corte = frontera_y < arriba ? arriba : (frontera_y > abajo ? abajo : frontera_y);
  layer_set_frame(s_numero_sobre, GRect(s_area_numero.origin.x, arriba, s_area_numero.size.w, corte - arriba));
  layer_set_frame(s_numero_bajo, GRect(s_area_numero.origin.x, corte, s_area_numero.size.w, abajo - corte));
}

static void canvas_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  int frontera_y = s_frontera_y;

  // La cortina cubre toda la pantalla y se revela desde arriba
  graphics_context_set_fill_color(ctx, s_fg_color);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);
  if (frontera_y > 0) {
    graphics_context_set_fill_color(ctx, s_bg_color);
    graphics_fill_rect(ctx, GRect(0, 0, bounds.size.w, frontera_y), 0, GCornerNone);
  }

  int h_title = bounds.size.h * 14 / 100;
  int h_status = bounds.size.h * 16 / 100;
  GRect title_area = GRect(0, PBL_IF_ROUND_ELSE(bounds.size.h / 14, 0), bounds.size.w, h_title);
  GRect status_area = GRect(0, bounds.size.h - h_status - PBL_IF_ROUND_ELSE(bounds.size.h / 14, 0), bounds.size.w, h_status);
  GRect big_area = GRect(0, h_title, bounds.size.w, bounds.size.h - h_title - h_status);

  graphics_context_set_text_color(ctx, color_por_y(title_area.origin.y + h_title / 2, frontera_y, s_fg_color, s_bg_color));
  graphics_draw_text(ctx, T->titulo, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
    title_area, GTextOverflowModeFill, GTextAlignmentCenter, NULL);

  graphics_context_set_text_color(ctx, color_por_y(status_area.origin.y + h_status / 2, frontera_y, s_fg_color, s_bg_color));
  graphics_draw_text(ctx, s_status_buf, fonts_get_system_font(FONT_KEY_GOTHIC_18),
    status_area, GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);

  if (s_eligiendo_modo) {
    GRect modo_area = GRect(big_area.origin.x, big_area.origin.y + big_area.size.h / 2 - 20, big_area.size.w, 40);
    graphics_context_set_text_color(ctx, color_por_y(modo_area.origin.y + 20, frontera_y, s_fg_color, s_bg_color));
    graphics_draw_text(ctx, s_modo == 0 ? T->normal : T->rapido, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD),
      modo_area, GTextOverflowModeFill, GTextAlignmentCenter, NULL);
  }
}

// Dibuja el número centrado en s_area_numero; la capa recorta su mitad.
static void dibujar_numero(Layer *layer, GContext *ctx, GColor color) {
  const char *texto = texto_numero();
  if (!texto) return;
  GRect marco = layer_get_frame(layer);
  // Caja en coordenadas de la capa: la misma posición absoluta en ambas capas.
  GRect caja = GRect(0, s_area_numero.origin.y - marco.origin.y, s_area_numero.size.w, s_area_numero.size.h);
  GSize tam = graphics_text_layout_get_content_size(texto, s_fuente_numeros, caja,
                                                    GTextOverflowModeFill, GTextAlignmentCenter);
  // Las fuentes de Pebble dejan aire arriba de los dígitos: se compensa para centrar el trazo visible.
  int aire_arriba = tam.h * 22 / 100;
  caja.origin.y += (caja.size.h - tam.h) / 2 - aire_arriba / 2;
  graphics_context_set_text_color(ctx, color);
  graphics_draw_text(ctx, texto, s_fuente_numeros, caja, GTextOverflowModeFill, GTextAlignmentCenter, NULL);
}

static void numero_sobre_update(Layer *layer, GContext *ctx) { dibujar_numero(layer, ctx, s_fg_color); }
static void numero_bajo_update(Layer *layer, GContext *ctx) { dibujar_numero(layer, ctx, s_bg_color); }

// ===================== Barra de acciones (derecha) =====================
static void draw_play_icon(GContext *ctx, int cx, int cy, GColor color) {
  GPoint pts[3] = { GPoint(cx - 5, cy - 7), GPoint(cx - 5, cy + 7), GPoint(cx + 7, cy) };
  GPathInfo info = { .num_points = 3, .points = pts };
  GPath *path = gpath_create(&info);
  graphics_context_set_fill_color(ctx, color);
  gpath_draw_filled(ctx, path);
  gpath_destroy(path);
}

static void draw_pause_icon(GContext *ctx, int cx, int cy, GColor color) {
  graphics_context_set_fill_color(ctx, color);
  graphics_fill_rect(ctx, GRect(cx - 6, cy - 7, 4, 14), 0, GCornerNone);
  graphics_fill_rect(ctx, GRect(cx + 2, cy - 7, 4, 14), 0, GCornerNone);
}

static void draw_gear_icon(GContext *ctx, int cx, int cy, GColor color, GColor hole_color) {
  graphics_context_set_fill_color(ctx, color);
  graphics_fill_circle(ctx, GPoint(cx, cy), 7);
  graphics_fill_rect(ctx, GRect(cx - 1, cy - 10, 2, 4), 0, GCornerNone);
  graphics_fill_rect(ctx, GRect(cx - 1, cy + 6, 2, 4), 0, GCornerNone);
  graphics_fill_rect(ctx, GRect(cx - 10, cy - 1, 4, 2), 0, GCornerNone);
  graphics_fill_rect(ctx, GRect(cx + 6, cy - 1, 4, 2), 0, GCornerNone);
  graphics_context_set_fill_color(ctx, hole_color);
  graphics_fill_circle(ctx, GPoint(cx, cy), 3);
}

static void draw_chevron_icon(GContext *ctx, int cx, int cy, GColor color, bool arriba) {
  GPoint pts[3];
  if (arriba) {
    pts[0] = GPoint(cx - 7, cy + 5); pts[1] = GPoint(cx + 7, cy + 5); pts[2] = GPoint(cx, cy - 5);
  } else {
    pts[0] = GPoint(cx - 7, cy - 5); pts[1] = GPoint(cx + 7, cy - 5); pts[2] = GPoint(cx, cy + 5);
  }
  GPathInfo info = { .num_points = 3, .points = pts };
  GPath *path = gpath_create(&info);
  graphics_context_set_fill_color(ctx, color);
  gpath_draw_filled(ctx, path);
  gpath_destroy(path);
}

static void action_bar_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  int w = bounds.size.w;
  int h = bounds.size.h;
  GPoint pos[3];        // iconos de arriba, select y abajo
  GColor tinta[3], fondo[3];
#if defined(PBL_ROUND)
  // En redondo no hay barra (el borde la recortaba): cada icono va junto a su
  // botón, siguiendo el círculo, y toma el color del texto en esa altura.
  int radio = w / 2 - 18;
  static const int angulos[3] = { 50, 90, 130 };   // grados desde las 12
  for (int i = 0; i < 3; i++) {
    int32_t a = DEG_TO_TRIGANGLE(angulos[i]);
    pos[i] = GPoint(w / 2 + sin_lookup(a) * radio / TRIG_MAX_RATIO,
                    h / 2 - cos_lookup(a) * radio / TRIG_MAX_RATIO);
    tinta[i] = color_por_y(pos[i].y, s_frontera_y, s_fg_color, s_bg_color);
    fondo[i] = color_por_y(pos[i].y, s_frontera_y, s_bg_color, s_fg_color);
  }
#else
  int cell_h = h / 3;
  graphics_context_set_fill_color(ctx, s_fg_color);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);
  graphics_context_set_stroke_color(ctx, s_bg_color);
  graphics_context_set_stroke_width(ctx, 1);
  graphics_draw_line(ctx, GPoint(0, cell_h), GPoint(w, cell_h));
  graphics_draw_line(ctx, GPoint(0, cell_h * 2), GPoint(w, cell_h * 2));
  for (int i = 0; i < 3; i++) {
    pos[i] = GPoint(w / 2, cell_h * i + cell_h / 2);
    tinta[i] = s_bg_color;
    fondo[i] = s_fg_color;
  }
#endif

  bool inactivo = !s_ciclo_activo && !s_cuenta_regresiva_activa && !s_eligiendo_modo;

  if (s_ciclo_activo) {
    if (s_pausado) draw_play_icon(ctx, pos[0].x, pos[0].y, tinta[0]);
    else draw_pause_icon(ctx, pos[0].x, pos[0].y, tinta[0]);
  } else if (s_eligiendo_modo) {
    draw_chevron_icon(ctx, pos[0].x, pos[0].y, tinta[0], true);
  }

  if (inactivo || s_eligiendo_modo) {
    draw_play_icon(ctx, pos[1].x, pos[1].y, tinta[1]);
  }

  if (inactivo) {
    draw_gear_icon(ctx, pos[2].x, pos[2].y, tinta[2], fondo[2]);
  } else if (s_eligiendo_modo) {
    draw_chevron_icon(ctx, pos[2].x, pos[2].y, tinta[2], false);
  }
}

// ===================== Persistencia del avance real =====================
// Los ms se guardan en dos enteros: segundos (como en la 2.2) y milisegundos aparte.
static void guardar_ms(uint32_t clave_s, uint32_t clave_ms, int64_t valor) {
  persist_write_int(clave_s, (int32_t)(valor / 1000));
  persist_write_int(clave_ms, (int32_t)(valor % 1000));
}

static int64_t leer_ms(uint32_t clave_s, uint32_t clave_ms) {
  return (int64_t)persist_read_int(clave_s) * 1000 + persist_read_int(clave_ms);
}

static void guardar_estado_ciclo(void) {
  persist_write_bool(KEY_CICLO_ACTIVO, s_ciclo_activo);
  guardar_ms(KEY_INICIO_EPOCH, KEY_INICIO_MS, s_inicio_ms);
  persist_write_bool(KEY_PAUSADO, s_pausado);
  guardar_ms(KEY_PAUSA_INICIO_EPOCH, KEY_PAUSA_INICIO_MS, s_pausa_inicio_ms);
  persist_write_int(KEY_ACUM_PAUSADO, s_acumulado_pausado_ms);
  persist_write_int(KEY_DURACION_SEGMENTO, s_duracion_segmento);
}

static void borrar_estado_ciclo(void) {
  persist_write_bool(KEY_CICLO_ACTIVO, false);
}

static void vibrar_patron_completado(void) {
  static const uint32_t segments[] = { 200, 100, 200, 100, 200 };
  VibePattern pattern = { .durations = segments, .num_segments = ARRAY_LENGTH(segments) };
  vibes_enqueue_custom_pattern(pattern);
}

// Pebble solo admite wakeups separados al menos 1 minuto, así que en vez de uno
// por tramo se programa uno solo, al salir de la app, para el próximo cambio de
// tramo. Al despertar la app sigue abierta y continúa el ciclo con normalidad.
static void programar_wakeup_al_salir(void) {
  wakeup_cancel_all();
  if (!s_ciclo_activo || s_pausado) return;
  int32_t dur_ms = s_duracion_segmento * 1000;
  int32_t t = transcurridos_ms();
  int32_t proximo = (t / dur_ms + 1) * dur_ms;
  time_t cuando = (time_t)((s_inicio_ms + s_acumulado_pausado_ms + proximo + 999) / 1000);
  time_t ahora = time(NULL);
  if (cuando <= ahora) cuando = ahora + 1;
  WakeupId id = wakeup_schedule(cuando, 0, true);
  if (id < 0) APP_LOG(APP_LOG_LEVEL_WARNING, "wakeup_schedule falló: %d", (int)id);
}

// ===================== Lógica del ciclo =====================
static void detener_ciclo(bool completado) {
  s_ciclo_activo = false;
  s_pausado = false;
  s_completado = completado;
  if (!completado) s_segmento_actual = 0;   // cancelado: vuelve a la pantalla inicial
  s_confirmando_detener = false;
  if (s_tick_timer) {
    app_timer_cancel(s_tick_timer);
    s_tick_timer = NULL;
  }
  wakeup_cancel_all();
  borrar_estado_ciclo();
  if (completado) vibrar_patron_completado();
  actualizar_status();
  refrescar_lienzo();
  refrescar_action_bar();
}

// Recalcula tramo y segundos restantes desde la hora de inicio.
// Devuelve true si el ciclo ya terminó.
static bool calcular_avance(void) {
  int32_t t = transcurridos_ms();
  if (t >= total_ms()) {
    s_segmento_actual = TOTAL_SEGMENTOS;
    s_segundos_restantes_seg = 0;
    return true;
  }
  int32_t dur_ms = s_duracion_segmento * 1000;
  s_segmento_actual = t / dur_ms + 1;
  s_segundos_restantes_seg = (dur_ms - t % dur_ms + 999) / 1000;
  return false;
}

static void tick_handler(void *data) {
  s_tick_timer = NULL;
  if (s_pausado) return;
  int tramo_previo = s_segmento_actual;
  if (calcular_avance()) {
    refrescar_lienzo();
    detener_ciclo(true);
    return;
  }
  if (s_segmento_actual != tramo_previo) {
    vibes_double_pulse();
    actualizar_status();
  }
  refrescar_lienzo();
  // Próximo tick justo al cambiar de segundo, contado desde el inicio del ciclo.
  int32_t espera = 1000 - transcurridos_ms() % 1000 + 5;
  s_tick_timer = app_timer_register(espera, tick_handler, NULL);
}

static void reanudar_ticks(void) {
  if (s_tick_timer) app_timer_cancel(s_tick_timer);
  s_tick_timer = NULL;
  tick_handler(NULL);
}

static void iniciar_ciclo(void) {
  s_ciclo_activo = true;
  s_pausado = false;
  s_completado = false;
  s_inicio_ms = ahora_ms();
  s_acumulado_pausado_ms = 0;
  calcular_avance();

  guardar_estado_ciclo();
  vibes_double_pulse();

  actualizar_status();
  refrescar_lienzo();
  refrescar_action_bar();
  reanudar_ticks();
}

static void cargar_estado_ciclo(bool lanzado_por_wakeup) {
  bool ciclo_guardado = persist_exists(KEY_CICLO_ACTIVO) ? persist_read_bool(KEY_CICLO_ACTIVO) : false;
  if (!ciclo_guardado) {
    s_ciclo_activo = false;
    s_completado = false;
    s_segmento_actual = 0;
    return;
  }

  s_inicio_ms = leer_ms(KEY_INICIO_EPOCH, KEY_INICIO_MS);
  s_pausado = persist_read_bool(KEY_PAUSADO);
  s_acumulado_pausado_ms = persist_read_int(KEY_ACUM_PAUSADO);
  s_duracion_segmento = persist_exists(KEY_DURACION_SEGMENTO) ? persist_read_int(KEY_DURACION_SEGMENTO) : DURACION_NORMAL_S;
  if (s_duracion_segmento != DURACION_RAPIDO_S) s_duracion_segmento = DURACION_NORMAL_S;
  if (s_pausado) s_pausa_inicio_ms = leer_ms(KEY_PAUSA_INICIO_EPOCH, KEY_PAUSA_INICIO_MS);
  // Un ciclo guardado por la 2.2 tenía la pausa en segundos y sin milisegundos.
  if (!persist_exists(KEY_INICIO_MS)) s_acumulado_pausado_ms *= 1000;

  if (calcular_avance()) {
    s_ciclo_activo = false;
    s_pausado = false;
    s_completado = true;
    borrar_estado_ciclo();
    if (lanzado_por_wakeup) vibrar_patron_completado();
    return;
  }

  s_ciclo_activo = true;
  s_completado = false;
  if (!s_pausado) {
    reanudar_ticks();
    if (lanzado_por_wakeup) vibes_double_pulse();
  }
}

// ===================== Selección de modo y cuenta regresiva =====================
static void countdown_tick_handler(void *data) {
  s_countdown_timer = NULL;
  s_cuenta_regresiva_valor--;
  if (s_cuenta_regresiva_valor <= 0) {
    s_cuenta_regresiva_activa = false;
    iniciar_ciclo();
    return;
  }
  refrescar_lienzo();
  vibes_short_pulse();
  s_countdown_timer = app_timer_register(1000, countdown_tick_handler, NULL);
}

static void iniciar_cuenta_regresiva(void) {
  s_cuenta_regresiva_activa = true;
  s_cuenta_regresiva_valor = CUENTA_REGRESIVA_S;
  s_completado = false;

  actualizar_status();
  refrescar_lienzo();
  refrescar_action_bar();

  vibes_short_pulse();
  if (s_countdown_timer) app_timer_cancel(s_countdown_timer);
  s_countdown_timer = app_timer_register(1000, countdown_tick_handler, NULL);
}

static void entrar_seleccion_modo(void) {
  s_eligiendo_modo = true;
  s_modo = (s_duracion_segmento == DURACION_RAPIDO_S) ? 1 : 0;
  actualizar_status();
  refrescar_lienzo();
  refrescar_action_bar();
}

static void alternar_modo(void) {
  s_modo = s_modo == 0 ? 1 : 0;
  actualizar_status();
  refrescar_lienzo();
}

static void confirmar_modo_y_empezar(void) {
  s_eligiendo_modo = false;
  s_duracion_segmento = (s_modo == 0) ? DURACION_NORMAL_S : DURACION_RAPIDO_S;
  iniciar_cuenta_regresiva();
}

// ===================== Ventana de configuración (colores) =====================
static uint16_t settings_get_num_sections_cb(MenuLayer *menu_layer, void *context) { return 2; }
static uint16_t settings_get_num_rows_cb(MenuLayer *menu_layer, uint16_t section_index, void *context) { return NUM_COLORS; }
static int16_t settings_get_header_height_cb(MenuLayer *menu_layer, uint16_t section_index, void *context) { return MENU_CELL_BASIC_HEADER_HEIGHT; }

static void settings_draw_header_cb(GContext *ctx, const Layer *cell_layer, uint16_t section_index, void *context) {
  menu_cell_basic_header_draw(ctx, cell_layer, section_index == 0 ? T->color_fondo : T->color_letras);
}

static void settings_draw_row_cb(GContext *ctx, const Layer *cell_layer, MenuIndex *cell_index, void *context) {
  GRect bounds = layer_get_bounds(cell_layer);
  // La fila resaltada tiene fondo oscuro: texto y marcas en blanco para que se lean.
  GColor tinta = menu_cell_layer_is_highlighted(cell_layer) ? GColorWhite : GColorBlack;
  GRect muestra = GRect(PBL_IF_ROUND_ELSE(22, 6), (bounds.size.h - 18) / 2, 18, 18);
  graphics_context_set_fill_color(ctx, PALETTE[cell_index->row]);
  graphics_fill_rect(ctx, muestra, 3, GCornersAll);
  graphics_context_set_stroke_color(ctx, tinta);
  graphics_draw_round_rect(ctx, muestra, 3);

  graphics_context_set_text_color(ctx, tinta);
  graphics_draw_text(ctx, T->colores[cell_index->row], fonts_get_system_font(FONT_KEY_GOTHIC_18),
    GRect(muestra.origin.x + 26, (bounds.size.h - 24) / 2, bounds.size.w - muestra.origin.x - 50, 24),
    GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);

  int selected = (cell_index->section == 0) ? s_bg_index : s_fg_index;
  if (cell_index->row == selected) {
    graphics_context_set_fill_color(ctx, tinta);
    graphics_fill_circle(ctx, GPoint(bounds.size.w - PBL_IF_ROUND_ELSE(30, 14), bounds.size.h / 2), 4);
  }
}

static void settings_select_click_cb(MenuLayer *menu_layer, MenuIndex *cell_index, void *context) {
  if (cell_index->section == 0) {
    s_bg_index = cell_index->row;
    persist_write_int(KEY_BG_INDEX, s_bg_index);
  } else {
    s_fg_index = cell_index->row;
    persist_write_int(KEY_FG_INDEX, s_fg_index);
  }
  apply_theme();
  menu_layer_reload_data(menu_layer);
}

static void settings_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  s_menu_layer = menu_layer_create(bounds);
  MenuLayerCallbacks callbacks = {
    .get_num_sections = settings_get_num_sections_cb,
    .get_num_rows = settings_get_num_rows_cb,
    .get_header_height = settings_get_header_height_cb,
    .draw_header = settings_draw_header_cb,
    .draw_row = settings_draw_row_cb,
    .select_click = settings_select_click_cb,
  };
  menu_layer_set_callbacks(s_menu_layer, NULL, callbacks);
  menu_layer_set_click_config_onto_window(s_menu_layer, window);
  layer_add_child(window_layer, menu_layer_get_layer(s_menu_layer));
}

static void settings_window_unload(Window *window) {
  menu_layer_destroy(s_menu_layer);
  window_destroy(window);
  s_settings_window = NULL;
}

static void abrir_settings(void) {
  s_settings_window = window_create();
  window_set_window_handlers(s_settings_window, (WindowHandlers) {
    .load = settings_window_load,
    .unload = settings_window_unload,
  });
  window_stack_push(s_settings_window, true);
}

// ===================== Botones =====================
static void select_click_handler(ClickRecognizerRef recognizer, void *context) {
  if (s_eligiendo_modo) {
    confirmar_modo_y_empezar();
  } else if (!s_ciclo_activo && !s_cuenta_regresiva_activa) {
    entrar_seleccion_modo();
  }
}

static void up_click_handler(ClickRecognizerRef recognizer, void *context) {
  if (s_ciclo_activo) {
    if (!s_pausado) {
      s_pausa_inicio_ms = ahora_ms();
      s_pausado = true;
      if (s_tick_timer) { app_timer_cancel(s_tick_timer); s_tick_timer = NULL; }
      vibes_double_pulse();
    } else {
      s_acumulado_pausado_ms += (int32_t)(ahora_ms() - s_pausa_inicio_ms);
      s_pausado = false;
      reanudar_ticks();
      vibes_short_pulse();
    }
    guardar_estado_ciclo();
    actualizar_status();
    refrescar_action_bar();
    refrescar_lienzo();
  } else if (s_eligiendo_modo) {
    alternar_modo();
  }
}

static void down_click_handler(ClickRecognizerRef recognizer, void *context) {
  if (s_eligiendo_modo) {
    alternar_modo();
  } else if (!s_ciclo_activo && !s_cuenta_regresiva_activa) {
    abrir_settings();
  }
}

static void fin_confirmacion(void *data) {
  s_confirmar_timer = NULL;
  s_confirmando_detener = false;
  actualizar_status();
  refrescar_lienzo();
}

static void back_click_handler(ClickRecognizerRef recognizer, void *context) {
  if (s_ciclo_activo) {
    // Un toque accidental no debe cortar el cepillado: hay que pulsar dos veces.
    if (!s_confirmando_detener) {
      s_confirmando_detener = true;
      s_confirmar_timer = app_timer_register(CONFIRMAR_DETENER_MS, fin_confirmacion, NULL);
      vibes_short_pulse();
      actualizar_status();
      refrescar_lienzo();
      return;
    }
    if (s_confirmar_timer) { app_timer_cancel(s_confirmar_timer); s_confirmar_timer = NULL; }
    detener_ciclo(false);
    vibes_short_pulse();
  } else if (s_cuenta_regresiva_activa) {
    s_cuenta_regresiva_activa = false;
    if (s_countdown_timer) {
      app_timer_cancel(s_countdown_timer);
      s_countdown_timer = NULL;
    }
    actualizar_status();
    refrescar_lienzo();
    refrescar_action_bar();
  } else if (s_eligiendo_modo) {
    s_eligiendo_modo = false;
    actualizar_status();
    refrescar_lienzo();
    refrescar_action_bar();
  } else {
    window_stack_pop(true);
  }
}

static void click_config_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_SELECT, select_click_handler);
  window_single_click_subscribe(BUTTON_ID_UP, up_click_handler);
  window_single_click_subscribe(BUTTON_ID_DOWN, down_click_handler);
  window_single_click_subscribe(BUTTON_ID_BACK, back_click_handler);
}

// ===================== Touch =====================
#if defined(PBL_TOUCH)
static GPoint s_touch_inicio;

static void tocar_para_confirmar(void) {
  if (s_eligiendo_modo) {
    confirmar_modo_y_empezar();
  } else if (!s_ciclo_activo && !s_cuenta_regresiva_activa) {
    entrar_seleccion_modo();
  }
}

static void touch_handler(const TouchEvent *event, void *context) {
  switch (event->type) {
    case TouchEvent_Touchdown:
      s_touch_inicio = GPoint(event->x, event->y);
      break;
    case TouchEvent_Liftoff: {
      int dx = event->x - s_touch_inicio.x;
      int dy = event->y - s_touch_inicio.y;
      if ((dx * dx + dy * dy) < 400) tocar_para_confirmar();
      break;
    }
    default:
      break;
  }
}
#endif

static void window_appear(Window *window) {
#if defined(PBL_TOUCH)
  if (touch_service_is_enabled()) {
    window_set_touch_bridge_disabled(window, true);
    touch_service_subscribe(touch_handler, NULL);
  }
#endif
}

static void window_disappear(Window *window) {
#if defined(PBL_TOUCH)
  touch_service_unsubscribe();
#endif
}

// ===================== Ventana principal =====================
static void window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);
  // En redondo los iconos flotan sobre el lienzo, que ocupa todo el ancho.
  int content_w = PBL_IF_ROUND_ELSE(bounds.size.w, bounds.size.w - MI_ACTION_BAR_WIDTH);
  int h = bounds.size.h;

  s_canvas_layer = layer_create(GRect(0, 0, content_w, h));
  layer_set_update_proc(s_canvas_layer, canvas_update_proc);
  layer_add_child(window_layer, s_canvas_layer);

  // Zona del número: entre el título y el estado, igual que en canvas_update_proc.
  int h_title = h * 14 / 100, h_status = h * 16 / 100;
  s_area_numero = GRect(0, h_title, content_w, h - h_title - h_status);
  s_numero_sobre = layer_create(s_area_numero);
  s_numero_bajo = layer_create(s_area_numero);
  layer_set_update_proc(s_numero_sobre, numero_sobre_update);
  layer_set_update_proc(s_numero_bajo, numero_bajo_update);
  layer_add_child(s_canvas_layer, s_numero_sobre);
  layer_add_child(s_canvas_layer, s_numero_bajo);

  s_action_bar_layer = layer_create(PBL_IF_ROUND_ELSE(bounds, GRect(content_w, 0, MI_ACTION_BAR_WIDTH, h)));
  layer_set_update_proc(s_action_bar_layer, action_bar_update_proc);
  layer_add_child(window_layer, s_action_bar_layer);

  apply_theme();
  actualizar_status();
}

static void window_unload(Window *window) {
  layer_destroy(s_numero_sobre);
  layer_destroy(s_numero_bajo);
  s_numero_sobre = s_numero_bajo = NULL;
  layer_destroy(s_canvas_layer);
  layer_destroy(s_action_bar_layer);
}

// ===================== Ciclo de vida =====================
static void cargar_preferencias(void) {
  s_bg_index = persist_exists(KEY_BG_INDEX) ? persist_read_int(KEY_BG_INDEX) : 0;
  s_fg_index = persist_exists(KEY_FG_INDEX) ? persist_read_int(KEY_FG_INDEX) : (NUM_COLORS > 1 ? 1 : 0);
  if (s_bg_index < 0 || s_bg_index >= NUM_COLORS) s_bg_index = 0;
  if (s_fg_index < 0 || s_fg_index >= NUM_COLORS) s_fg_index = (NUM_COLORS > 1) ? 1 : 0;
}

static void init(void) {
  if (strncmp(i18n_get_system_locale(), "es", 2) == 0) T = &ES;
  bool lanzado_por_wakeup = (launch_reason() == APP_LAUNCH_WAKEUP);
  wakeup_cancel_all();   // con la app abierta los avisos los da ella misma

  s_fuente_numeros = fonts_load_custom_font(resource_get_handle(FUENTE_NUMEROS));
  cargar_preferencias();
  cargar_estado_ciclo(lanzado_por_wakeup);
#ifdef CAPTURAS
  s_ciclo_activo = true;
  s_pausado = false;
  calcular_avance();
  actualizar_status();
#endif

#if defined(PBL_TOUCH)
  app_touch_navigation_enable(true);
#endif

  s_window = window_create();
  window_set_click_config_provider(s_window, click_config_provider);
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load = window_load,
    .unload = window_unload,
    .appear = window_appear,
    .disappear = window_disappear,
  });
  window_stack_push(s_window, true);
}

static void deinit(void) {
  if (s_ciclo_activo) guardar_estado_ciclo();
  programar_wakeup_al_salir();
  if (s_tick_timer) app_timer_cancel(s_tick_timer);
  if (s_countdown_timer) app_timer_cancel(s_countdown_timer);
  if (s_confirmar_timer) app_timer_cancel(s_confirmar_timer);
  window_destroy(s_window);
  fonts_unload_custom_font(s_fuente_numeros);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}

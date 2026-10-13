#include <pebble.h>

#define TOTAL_SEGMENTOS 6
#define DURACION_NORMAL_S 30
#define DURACION_RAPIDO_S 15
#define TICK_MS 1000
#define CUENTA_REGRESIVA_S 3
#define MI_ACTION_BAR_WIDTH 30

#define KEY_BG_INDEX 100
#define KEY_FG_INDEX 101
#define KEY_CICLO_ACTIVO 102
#define KEY_INICIO_EPOCH 103
#define KEY_PAUSADO 104
#define KEY_PAUSA_INICIO_EPOCH 105
#define KEY_ACUM_PAUSADO 106
#define KEY_DURACION_SEGMENTO 107

#if defined(PBL_COLOR)
  #define NUM_COLORS 10
  static const GColor PALETTE[NUM_COLORS] = {
    GColorWhite, GColorBlack, GColorBlue, GColorGreen,
    GColorRed, GColorYellow, GColorOrange, GColorPurple,
    GColorMagenta, GColorLightGray
  };
  static const char *PALETTE_NAMES[NUM_COLORS] = {
    "Blanco", "Negro", "Azul", "Verde",
    "Rojo", "Amarillo", "Naranja", "Morado",
    "Magenta", "Gris claro"
  };
#else
  #define NUM_COLORS 2
  static const GColor PALETTE[NUM_COLORS] = { GColorWhite, GColorBlack };
  static const char *PALETTE_NAMES[NUM_COLORS] = { "Blanco", "Negro" };
#endif

// Tabla de 7 segmentos: a,b,c,d,e,f,g
static const bool DIGIT_SEGMENTS[10][7] = {
  {1,1,1,1,1,1,0}, {0,1,1,0,0,0,0}, {1,1,0,1,1,0,1}, {1,1,1,1,0,0,1},
  {0,1,1,0,0,1,1}, {1,0,1,1,0,1,1}, {1,0,1,1,1,1,1}, {1,1,1,0,0,0,0},
  {1,1,1,1,1,1,1}, {1,1,1,1,0,1,1}
};

static int s_bg_index = 0;
static int s_fg_index = 1;
static GColor s_bg_color;
static GColor s_fg_color;

static Window *s_window;
static Layer *s_canvas_layer;
static Layer *s_action_bar_layer;

static AppTimer *s_tick_timer;
static AppTimer *s_countdown_timer;

static int s_segmento_actual = 0;
static int s_segundos_restantes_seg = 0;
static bool s_ciclo_activo = false;
static bool s_pausado = false;
static bool s_completado = false;

static bool s_cuenta_regresiva_activa = false;
static int s_cuenta_regresiva_valor = 0;

static bool s_eligiendo_modo = false;
static int s_modo = 0; // 0 = normal (30s), 1 = rápido (15s)
static int s_duracion_segmento = DURACION_NORMAL_S;

static time_t s_inicio_epoch = 0;
static time_t s_pausa_inicio_epoch = 0;
static int s_acumulado_pausado = 0;

static char s_status_buf[36];

static Window *s_settings_window;
static MenuLayer *s_menu_layer;

// ===================== Tema de colores =====================
static void apply_theme(void) {
  s_bg_color = PALETTE[s_bg_index];
  s_fg_color = PALETTE[s_fg_index];
  if (s_window) window_set_background_color(s_window, s_bg_color);
  if (s_canvas_layer) layer_mark_dirty(s_canvas_layer);
  if (s_action_bar_layer) layer_mark_dirty(s_action_bar_layer);
}

static void refrescar_lienzo(void) { if (s_canvas_layer) layer_mark_dirty(s_canvas_layer); }
static void refrescar_action_bar(void) { if (s_action_bar_layer) layer_mark_dirty(s_action_bar_layer); }

// ===================== Texto de estatus =====================
static void actualizar_status(void) {
  if (s_eligiendo_modo) {
    snprintf(s_status_buf, sizeof(s_status_buf), s_modo == 0 ? "30s por lado" : "15s por lado");
  } else if (s_cuenta_regresiva_activa) {
    snprintf(s_status_buf, sizeof(s_status_buf), "Prepárate...");
  } else if (!s_ciclo_activo && s_segmento_actual == 0 && !s_completado) {
    snprintf(s_status_buf, sizeof(s_status_buf), "Toca o SELECT");
  } else if (s_ciclo_activo && s_pausado) {
    snprintf(s_status_buf, sizeof(s_status_buf), "Pausado (%d de %d)", s_segmento_actual, TOTAL_SEGMENTOS);
  } else if (s_ciclo_activo) {
    snprintf(s_status_buf, sizeof(s_status_buf), "Tramo %d de %d", s_segmento_actual, TOTAL_SEGMENTOS);
  } else {
    snprintf(s_status_buf, sizeof(s_status_buf), "Listo, cepillado!");
  }
}

// ===================== Lienzo principal (cortina + texto + número) =====================
static GColor color_por_y(int y_centro, int frontera_y, GColor sobre, GColor bajo) {
  return (y_centro < frontera_y) ? sobre : bajo;
}

static void dibujar_segmento(GContext *ctx, GRect r, int frontera_y, GColor sobre, GColor bajo) {
  int centro = r.origin.y + r.size.h / 2;
  graphics_context_set_fill_color(ctx, color_por_y(centro, frontera_y, sobre, bajo));
  graphics_fill_rect(ctx, r, 0, GCornerNone);
}

static void dibujar_digito(GContext *ctx, GRect box, int digito, int frontera_y, GColor sobre, GColor bajo) {
  if (digito < 0 || digito > 9) return;
  const bool *seg = DIGIT_SEGMENTS[digito];
  int w = box.size.w, h = box.size.h, x = box.origin.x, y = box.origin.y;
  int t = w / 5;
  if (t < 6) t = 6;
  int vh = h / 2 - t;
  if (vh < 2) vh = 2;

  if (seg[0]) dibujar_segmento(ctx, GRect(x + t / 2, y, w - t, t), frontera_y, sobre, bajo);
  if (seg[6]) dibujar_segmento(ctx, GRect(x + t / 2, y + h / 2 - t / 2, w - t, t), frontera_y, sobre, bajo);
  if (seg[3]) dibujar_segmento(ctx, GRect(x + t / 2, y + h - t, w - t, t), frontera_y, sobre, bajo);
  if (seg[5]) dibujar_segmento(ctx, GRect(x, y + (t * 6) / 10, t, vh), frontera_y, sobre, bajo);
  if (seg[1]) dibujar_segmento(ctx, GRect(x + w - t, y + (t * 6) / 10, t, vh), frontera_y, sobre, bajo);
  if (seg[4]) dibujar_segmento(ctx, GRect(x, y + h / 2 + (t * 4) / 10, t, vh), frontera_y, sobre, bajo);
  if (seg[2]) dibujar_segmento(ctx, GRect(x + w - t, y + h / 2 + (t * 4) / 10, t, vh), frontera_y, sobre, bajo);
}

static void dibujar_numero(GContext *ctx, GRect area, int valor, int frontera_y, GColor sobre, GColor bajo) {
  if (valor < 0) valor = 0;
  if (valor > 99) valor = 99;
  int decenas = valor / 10;
  int unidades = valor % 10;
  int len = (valor >= 10) ? 2 : 1;

  int max_w = (area.size.h * 6) / 10;
  int gap = (area.size.w * 6) / 100;
  int digit_w, total_w;
  if (len == 1) {
    digit_w = max_w;
    total_w = digit_w;
  } else {
    digit_w = (area.size.w - gap) / 2;
    if (digit_w > max_w) digit_w = max_w;
    total_w = digit_w * 2 + gap;
  }
  int start_x = area.origin.x + (area.size.w - total_w) / 2;

  if (len == 2) {
    dibujar_digito(ctx, GRect(start_x, area.origin.y, digit_w, area.size.h), decenas, frontera_y, sobre, bajo);
    dibujar_digito(ctx, GRect(start_x + digit_w + gap, area.origin.y, digit_w, area.size.h), unidades, frontera_y, sobre, bajo);
  } else {
    dibujar_digito(ctx, GRect(start_x, area.origin.y, digit_w, area.size.h), unidades, frontera_y, sobre, bajo);
  }
}

static void canvas_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  int total_segundos = TOTAL_SEGMENTOS * s_duracion_segmento;
  int transcurridos = 0;

  if (s_completado) {
    transcurridos = total_segundos;
  } else if (s_ciclo_activo) {
    transcurridos = (s_segmento_actual - 1) * s_duracion_segmento + (s_duracion_segmento - s_segundos_restantes_seg);
  }
  if (transcurridos < 0) transcurridos = 0;
  if (transcurridos > total_segundos) transcurridos = total_segundos;

  // La cortina cubre toda la pantalla y se revela desde arriba
  graphics_context_set_fill_color(ctx, s_fg_color);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);
  int frontera_y = (bounds.size.h * transcurridos) / total_segundos;
  if (frontera_y > 0) {
    graphics_context_set_fill_color(ctx, s_bg_color);
    graphics_fill_rect(ctx, GRect(0, 0, bounds.size.w, frontera_y), 0, GCornerNone);
  }

  int h_title = bounds.size.h * 14 / 100;
  int h_status = bounds.size.h * 16 / 100;
  GRect title_area = GRect(0, 0, bounds.size.w, h_title);
  GRect status_area = GRect(0, bounds.size.h - h_status, bounds.size.w, h_status);
  GRect big_area = GRect(0, h_title, bounds.size.w, bounds.size.h - h_title - h_status);

  GColor color_titulo = color_por_y(title_area.origin.y + h_title / 2, frontera_y, s_fg_color, s_bg_color);
  graphics_context_set_text_color(ctx, color_titulo);
  graphics_draw_text(ctx, "Cepillado", fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
    title_area, GTextOverflowModeFill, GTextAlignmentCenter, NULL);

  GColor color_status = color_por_y(status_area.origin.y + h_status / 2, frontera_y, s_fg_color, s_bg_color);
  graphics_context_set_text_color(ctx, color_status);
  graphics_draw_text(ctx, s_status_buf, fonts_get_system_font(FONT_KEY_GOTHIC_18),
    status_area, GTextOverflowModeFill, GTextAlignmentCenter, NULL);

  if (s_eligiendo_modo) {
    GColor color_modo = color_por_y(big_area.origin.y + big_area.size.h / 2, frontera_y, s_fg_color, s_bg_color);
    graphics_context_set_text_color(ctx, color_modo);
    GRect modo_area = GRect(big_area.origin.x, big_area.origin.y + big_area.size.h / 2 - 20, big_area.size.w, 40);
    graphics_draw_text(ctx, s_modo == 0 ? "NORMAL" : "RAPIDO", fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD),
      modo_area, GTextOverflowModeFill, GTextAlignmentCenter, NULL);
  } else if (s_cuenta_regresiva_activa) {
    dibujar_numero(ctx, big_area, s_cuenta_regresiva_valor, frontera_y, s_fg_color, s_bg_color);
  } else if (s_ciclo_activo) {
    dibujar_numero(ctx, big_area, s_segundos_restantes_seg, frontera_y, s_fg_color, s_bg_color);
  } else if (s_completado) {
    dibujar_numero(ctx, big_area, 0, frontera_y, s_fg_color, s_bg_color);
  }
}

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
  int cell_h = h / 3;
  int cx = w / 2;

  graphics_context_set_fill_color(ctx, s_fg_color);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);

  graphics_context_set_stroke_color(ctx, s_bg_color);
  graphics_context_set_stroke_width(ctx, 1);
  graphics_draw_line(ctx, GPoint(0, cell_h), GPoint(w, cell_h));
  graphics_draw_line(ctx, GPoint(0, cell_h * 2), GPoint(w, cell_h * 2));

  bool inactivo = !s_ciclo_activo && !s_cuenta_regresiva_activa && !s_eligiendo_modo;

  if (s_ciclo_activo) {
    int cy = cell_h / 2;
    if (s_pausado) draw_play_icon(ctx, cx, cy, s_bg_color);
    else draw_pause_icon(ctx, cx, cy, s_bg_color);
  } else if (s_eligiendo_modo) {
    draw_chevron_icon(ctx, cx, cell_h / 2, s_bg_color, true);
  }

  if (inactivo || s_eligiendo_modo) {
    int cy = cell_h + cell_h / 2;
    draw_play_icon(ctx, cx, cy, s_bg_color);
  }

  if (inactivo) {
    int cy = cell_h * 2 + cell_h / 2;
    draw_gear_icon(ctx, cx, cy, s_bg_color, s_fg_color);
  } else if (s_eligiendo_modo) {
    draw_chevron_icon(ctx, cx, cell_h * 2 + cell_h / 2, s_bg_color, false);
  }
}

// ===================== Persistencia del avance real =====================
static void guardar_estado_ciclo(void) {
  persist_write_bool(KEY_CICLO_ACTIVO, s_ciclo_activo);
  persist_write_int(KEY_INICIO_EPOCH, (int)s_inicio_epoch);
  persist_write_bool(KEY_PAUSADO, s_pausado);
  persist_write_int(KEY_PAUSA_INICIO_EPOCH, (int)s_pausa_inicio_epoch);
  persist_write_int(KEY_ACUM_PAUSADO, s_acumulado_pausado);
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

static void programar_wakeups_restantes(void) {
  wakeup_cancel_all();
  time_t base = s_inicio_epoch + s_acumulado_pausado;
  time_t ahora = time(NULL);
  for (int i = 1; i <= TOTAL_SEGMENTOS; i++) {
    time_t cuando = base + (i * s_duracion_segmento);
    if (cuando > ahora) wakeup_schedule(cuando, i, true);
  }
}

// ===================== Lógica del ciclo =====================
static void detener_ciclo(bool completado) {
  s_ciclo_activo = false;
  s_pausado = false;
  s_completado = completado;
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

static void tick_handler(void *data) {
  if (s_pausado) {
    s_tick_timer = app_timer_register(TICK_MS, tick_handler, NULL);
    return;
  }
  s_segundos_restantes_seg--;

  if (s_segundos_restantes_seg <= 0) {
    if (s_segmento_actual >= TOTAL_SEGMENTOS) {
      s_segundos_restantes_seg = 0;
      refrescar_lienzo();
      detener_ciclo(true);
      return;
    }
    vibes_double_pulse();
    s_segmento_actual++;
    s_segundos_restantes_seg = s_duracion_segmento;
    actualizar_status();
  }

  refrescar_lienzo();
  s_tick_timer = app_timer_register(TICK_MS, tick_handler, NULL);
}

static void iniciar_ciclo(void) {
  s_segmento_actual = 1;
  s_segundos_restantes_seg = s_duracion_segmento;
  s_ciclo_activo = true;
  s_pausado = false;
  s_completado = false;
  s_inicio_epoch = time(NULL);
  s_acumulado_pausado = 0;

  guardar_estado_ciclo();
  programar_wakeups_restantes();
  vibes_double_pulse();

  actualizar_status();
  refrescar_lienzo();
  refrescar_action_bar();

  if (s_tick_timer) app_timer_cancel(s_tick_timer);
  s_tick_timer = app_timer_register(TICK_MS, tick_handler, NULL);
}

static void cargar_estado_ciclo(bool lanzado_por_wakeup) {
  bool ciclo_guardado = persist_exists(KEY_CICLO_ACTIVO) ? persist_read_bool(KEY_CICLO_ACTIVO) : false;
  if (!ciclo_guardado) {
    s_ciclo_activo = false;
    s_completado = false;
    s_segmento_actual = 0;
    return;
  }

  s_inicio_epoch = (time_t)persist_read_int(KEY_INICIO_EPOCH);
  s_pausado = persist_read_bool(KEY_PAUSADO);
  s_acumulado_pausado = persist_read_int(KEY_ACUM_PAUSADO);
  s_duracion_segmento = persist_exists(KEY_DURACION_SEGMENTO) ? persist_read_int(KEY_DURACION_SEGMENTO) : DURACION_NORMAL_S;
  if (s_pausado) s_pausa_inicio_epoch = (time_t)persist_read_int(KEY_PAUSA_INICIO_EPOCH);

  time_t ahora = time(NULL);
  int transcurridos;
  if (s_pausado) transcurridos = (int)(s_pausa_inicio_epoch - s_inicio_epoch) - s_acumulado_pausado;
  else transcurridos = (int)(ahora - s_inicio_epoch) - s_acumulado_pausado;
  if (transcurridos < 0) transcurridos = 0;

  int total_segundos = TOTAL_SEGMENTOS * s_duracion_segmento;

  if (transcurridos >= total_segundos) {
    s_ciclo_activo = false;
    s_pausado = false;
    s_completado = true;
    s_segmento_actual = TOTAL_SEGMENTOS;
    s_segundos_restantes_seg = 0;
    borrar_estado_ciclo();
    if (lanzado_por_wakeup) vibrar_patron_completado();
    return;
  }

  s_ciclo_activo = true;
  s_completado = false;
  s_segmento_actual = (transcurridos / s_duracion_segmento) + 1;
  if (s_segmento_actual > TOTAL_SEGMENTOS) s_segmento_actual = TOTAL_SEGMENTOS;
  int seg_en_tramo = transcurridos % s_duracion_segmento;
  s_segundos_restantes_seg = s_duracion_segmento - seg_en_tramo;

  if (!s_pausado) {
    s_tick_timer = app_timer_register(TICK_MS, tick_handler, NULL);
    programar_wakeups_restantes();
    if (lanzado_por_wakeup) vibes_double_pulse();
  }
}

// ===================== Selección de modo y cuenta regresiva =====================
static void countdown_tick_handler(void *data) {
  s_cuenta_regresiva_valor--;
  if (s_cuenta_regresiva_valor <= 0) {
    s_cuenta_regresiva_activa = false;
    iniciar_ciclo();
    return;
  }
  refrescar_lienzo();
  vibes_short_pulse();
  s_countdown_timer = app_timer_register(TICK_MS, countdown_tick_handler, NULL);
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
  s_countdown_timer = app_timer_register(TICK_MS, countdown_tick_handler, NULL);
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
  menu_cell_basic_header_draw(ctx, cell_layer, section_index == 0 ? "Color de fondo" : "Color de letras");
}

static void settings_draw_row_cb(GContext *ctx, const Layer *cell_layer, MenuIndex *cell_index, void *context) {
  GRect bounds = layer_get_bounds(cell_layer);
  GColor swatch = PALETTE[cell_index->row];
  graphics_context_set_fill_color(ctx, swatch);
  graphics_fill_rect(ctx, GRect(6, (bounds.size.h - 18) / 2, 18, 18), 3, GCornersAll);

  graphics_context_set_text_color(ctx, GColorBlack);
  graphics_draw_text(ctx, PALETTE_NAMES[cell_index->row], fonts_get_system_font(FONT_KEY_GOTHIC_18),
    GRect(32, 0, bounds.size.w - 50, bounds.size.h), GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);

  int selected = (cell_index->section == 0) ? s_bg_index : s_fg_index;
  if (cell_index->row == selected) {
    graphics_context_set_fill_color(ctx, GColorBlack);
    graphics_fill_circle(ctx, GPoint(bounds.size.w - 14, bounds.size.h / 2), 4);
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
      s_pausado = true;
      s_pausa_inicio_epoch = time(NULL);
      wakeup_cancel_all();
      vibes_double_pulse();
    } else {
      s_acumulado_pausado += (int)(time(NULL) - s_pausa_inicio_epoch);
      s_pausado = false;
      programar_wakeups_restantes();
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

static void back_click_handler(ClickRecognizerRef recognizer, void *context) {
  if (s_ciclo_activo) {
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
  int content_w = bounds.size.w - MI_ACTION_BAR_WIDTH;
  int h = bounds.size.h;

  s_canvas_layer = layer_create(GRect(0, 0, content_w, h));
  layer_set_update_proc(s_canvas_layer, canvas_update_proc);
  layer_add_child(window_layer, s_canvas_layer);

  s_action_bar_layer = layer_create(GRect(content_w, 0, MI_ACTION_BAR_WIDTH, h));
  layer_set_update_proc(s_action_bar_layer, action_bar_update_proc);
  layer_add_child(window_layer, s_action_bar_layer);

  apply_theme();
  actualizar_status();
}

static void window_unload(Window *window) {
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
  bool lanzado_por_wakeup = (launch_reason() == APP_LAUNCH_WAKEUP);

  cargar_preferencias();
  cargar_estado_ciclo(lanzado_por_wakeup);

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
  if (s_tick_timer) app_timer_cancel(s_tick_timer);
  if (s_countdown_timer) app_timer_cancel(s_countdown_timer);
  window_destroy(s_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}

# Tútt Cepillado — watchapp para Pebble (SDK C)

Temporizador de cepillado dental para Pebble. Un solo archivo: `src/c/main.c`.
Publicada en la tienda: https://apps.repebble.com/65ac389e4d2e43a9b4e87658 (UUID `349dee92-a4d4-431f-93d5-6bc12c101adf`; no cambiarlo o se publicaría como otra app).

## Funcionamiento
- 6 tramos por ciclo. Modo Normal = 30 s por tramo (3 min), modo Rápido = 15 s por tramo.
- Flujo: SELECT o toque -> elegir modo (UP/DOWN alternan) -> SELECT/toque confirma -> cuenta regresiva de 3 s -> ciclo.
- Vibración al cambiar de tramo (y al empezar): 2 pulsos de 350 ms; al terminar: 3 de 500 ms. El SDK no tiene control de intensidad, así que se alargan los pulsos. Cuenta regresiva y pausa usan los pulsos cortos del sistema.
- Pantalla completa = cortina de progreso: el color de letras cubre todo y se revela el color de fondo de arriba hacia abajo.
- Título y estado cambian de color según queden sobre la cortina o sobre la zona revelada.
- Números con Fredoka SemiBold (solo dígitos), centrados. Se pintan dos veces en dos capas recortadas a cada lado de la cortina (`s_numero_sobre` / `s_numero_bajo`), así el número cambia de color justo donde la cruza.
- Barra lateral derecha (30 px) con iconos: play/pausa, engrane (colores), chevrons en la elección de modo. En pantallas redondas no hay barra: los iconos van junto a cada botón siguiendo el círculo.
- UP = pausa/reanuda durante el ciclo; DOWN = menú de temas (inactivo); BACK = detener (dos pulsaciones en 2,5 s durante el ciclo) / cancelar / salir.
- Menú de temas (MenuLayer): 8 pares fondo/cortina con contraste ≥ 7,6:1 en color (2 en blanco y negro), con fichas de vista previa; se guarda en KEY_TEMA. Al venir de la 2.2 (KEY_BG_INDEX/KEY_FG_INDEX) se elige el tema más parecido. No volver a colores sueltos: 58 de las 100 combinaciones de la 2.2 tenían contraste < 3.
- Touch (PBL_TOUCH): un toque corto equivale a SELECT.
- Textos en español o inglés según `i18n_get_system_locale()` (structs `ES` / `EN`).
- Continuidad: guarda inicio y pausas en ms (segundos y milisegundos en claves aparte) y recalcula el avance desde ahí en cada tick, sin restar contadores.
- Wakeups: Pebble exige 1 minuto entre wakeups (máx. 8), así que no se programa uno por tramo. Al salir de la app con el ciclo en marcha se programa uno solo para el próximo cambio de tramo; al despertar la app sigue abierta y continúa.

## Notas técnicas
- `-Werror` activo: cuidado con `const` duplicados y buffers de snprintf chicos.
- `ACTION_BAR_WIDTH` ya existe en pebble.h; la macro propia se llama `MI_ACTION_BAR_WIDTH`.
- `sdkVersion` en package.json siempre "3" (también con SDK 4 y touch); el SDK actual rechaza "4".
- Fuentes: el conversor limita cada glifo a 512 bytes en emery/gabbro y 256 en el resto. Por eso FREDOKA_GRANDE_96 (emery, gabbro) y FREDOKA_CHICA_72 (resto); más grande no compila.
- Plataformas: aplite, basalt, chalk, diorite, emery, flint, gabbro.
- `herramientas/capturas.sh` congela el ciclo a media pantalla (`#define CAPTURAS`) y saca las capturas de la tienda, con un tema distinto por modelo a color; hace `pebble clean` porque waf no nota el cambio de una línea.

## Compilar
    pebble build
    pebble install --emulator emery      # o --phone <IP>

## Publicar una versión nueva
Subir `version` en package.json y, desde la raíz:

    pebble publish --non-interactive --is-published --no-gif-all-platforms --release-notes "..."

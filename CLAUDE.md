# Cepillado — watchapp para Pebble (SDK C)

Temporizador de cepillado dental para Pebble. Un solo archivo: `src/c/main.c`.

## Funcionamiento
- 6 tramos por ciclo. Modo Normal = 30 s por tramo (3 min), modo Rápido = 15 s por tramo.
- Flujo: SELECT o toque -> elegir modo (UP/DOWN alternan) -> SELECT/toque confirma -> cuenta regresiva de 3 s -> ciclo.
- Vibración doble al cambiar de tramo; patrón de 3 pulsos al terminar.
- Pantalla completa = cortina de progreso: el color de letras cubre todo y se revela el color de fondo de arriba hacia abajo.
- Texto y números dibujados a mano en el canvas; su color se calcula según estén sobre la cortina o sobre la zona revelada (siempre legibles).
- Números gigantes estilo brutalista (7 segmentos dibujados con rectángulos).
- Barra lateral derecha (30 px) con iconos: play/pausa, engrane (colores), chevrons en la elección de modo.
- UP = pausa/reanuda durante el ciclo; DOWN = menú de colores (inactivo); BACK = detener/cancelar (inactivo cierra la app).
- Menú de colores (MenuLayer) con 10 colores en pantallas a color y 2 en blanco y negro; se guarda con persist.
- Touch (PBL_TOUCH): un toque corto equivale a SELECT.
- Continuidad: guarda hora de inicio (epoch) y pausas con persist; al reabrir recalcula el avance real. Wakeups programados por cada cambio de tramo.

## Notas técnicas
- `-Werror` activo: cuidado con `const` duplicados y buffers de snprintf chicos (usar >= 12 bytes para enteros).
- `ACTION_BAR_WIDTH` ya existe en pebble.h; la macro propia se llama `MI_ACTION_BAR_WIDTH`.
- Nombre del proyecto (displayName) corto: nombres largos rompen appinfo.auto.c.
- `sdkVersion` en package.json: "4" (necesario para touch).
- Plataformas: aplite, basalt, chalk, diorite, emery.

## Compilar
    pebble build
    pebble install --emulator emery      # o --phone <IP>

## Ideas pendientes (v2)
- Observaciones de uso real del dueño (pendientes de recopilar).
- Ícono visual para el botón BACK en pantalla (hoy no está indicado).

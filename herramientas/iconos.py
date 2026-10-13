#!/usr/bin/python3
"""Genera los iconos de Tútt: menú del reloj (25×25) y tienda (80×80 y 144×144).

Un diente con un anillo de progreso alrededor, en los colores del tema Solarizado.
"""
import math
from pathlib import Path
from PIL import Image, ImageDraw

RAIZ = Path(__file__).resolve().parent.parent
FONDO = (0, 85, 85, 255)        # GColorMidnightGreen
CREMA = (255, 255, 170, 255)    # GColorPastelYellow
TENUE = (0, 130, 130, 255)      # resto del anillo, apenas visible
ESCALA = 8                      # se dibuja grande y se reduce: bordes suaves

# Contorno del diente en una rejilla de 100×100 (sentido horario), suavizado con Catmull-Rom.
DIENTE = [(50, 22), (65, 13), (79, 18), (86, 32), (83, 54), (77, 79), (69, 90), (62, 79),
          (50, 65), (38, 79), (31, 90), (23, 79), (17, 54), (14, 32), (21, 18), (35, 13)]


def catmull_rom(puntos, pasos=12):
    n, curva = len(puntos), []
    for i in range(n):
        p0, p1, p2, p3 = (puntos[(i + k) % n] for k in (-1, 0, 1, 2))
        for s in range(pasos):
            t = s / pasos
            curva.append(tuple(0.5 * (2 * p1[j] + (-p0[j] + p2[j]) * t
                                      + (2 * p0[j] - 5 * p1[j] + 4 * p2[j] - p3[j]) * t * t
                                      + (-p0[j] + 3 * p1[j] - 3 * p2[j] + p3[j]) * t ** 3) for j in (0, 1)))
    return curva


def diente(d, cx, cy, tam, color):
    pts = [(cx + (x - 50) * tam / 100, cy + (y - 52) * tam / 100) for x, y in catmull_rom(DIENTE)]
    d.polygon(pts, fill=color)


def destello(d, cx, cy, r, color):
    # Estrella de cuatro puntas
    a, b = r, r * 0.28
    d.polygon([(cx, cy - a), (cx + b, cy - b), (cx + a, cy), (cx + b, cy + b),
               (cx, cy + a), (cx - b, cy + b), (cx - a, cy), (cx - b, cy - b)], fill=color)


def icono_tienda(lado):
    s = lado * ESCALA
    img = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    d.rounded_rectangle([0, 0, s - 1, s - 1], radius=s // 5, fill=FONDO)
    # Anillo de progreso: tres cuartos hechos, el resto tenue
    m, grosor = s * 0.13, max(int(s * 0.055), 1)
    caja = [m, m, s - m, s - m]
    d.arc(caja, 0, 360, fill=TENUE, width=grosor)
    d.arc(caja, -90, 180, fill=CREMA, width=grosor)
    # Punta redondeada al final del arco
    r = (s - 2 * m) / 2 - grosor / 2
    for ang in (-90, 180):
        x = s / 2 + r * math.cos(math.radians(ang))
        y = s / 2 + r * math.sin(math.radians(ang))
        d.ellipse([x - grosor / 2, y - grosor / 2, x + grosor / 2, y + grosor / 2], fill=CREMA)
    diente(d, s / 2, s / 2 + s * 0.02, s * 0.52, CREMA)
    destello(d, s * 0.72, s * 0.29, s * 0.038, CREMA)   # brillo junto al diente, sin tocarlo
    return img.resize((lado, lado), Image.LANCZOS)


def nitido(img, lado):
    """Reduce a lado×lado sin antialias: cada píxel queda negro opaco o transparente,
    como los iconos del sistema, que el lanzador invierte en la fila seleccionada."""
    alfa = img.resize((lado, lado), Image.LANCZOS).getchannel("A")
    alfa = alfa.point(lambda a: 255 if a >= 110 else 0)
    out = Image.new("RGBA", (lado, lado), (0, 0, 0, 0))
    out.putalpha(alfa)
    return out


def icono_menu(lado=25):
    # Estilo de los iconos de PebbleOS: contorno negro de ~2 px sobre fondo transparente.
    s = lado * ESCALA
    img = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    pts = [(s / 2 + (x - 50) * s * 0.86 / 100, s / 2 + (y - 51) * s * 0.86 / 100) for x, y in catmull_rom(DIENTE)]
    d.line(pts + pts[:2], fill=(0, 0, 0, 255), width=int(2.1 * ESCALA), joint="curve")
    return nitido(img, lado)


if __name__ == "__main__":
    icono_menu().save(RAIZ / "resources/images/icono-menu.png")
    icono_tienda(80).save(RAIZ / "tienda/icono-80.png")
    icono_tienda(144).save(RAIZ / "tienda/icono-144.png")
    print("Iconos generados")

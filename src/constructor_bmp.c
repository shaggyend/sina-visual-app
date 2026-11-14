#include "constructor_bmp.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned char *convertir_filas_a_grises(const unsigned char *datos_pixeles, size_t tamano_disponible,
                                               int w, int h, int arriba_abajo, int bits_pixel) {
    if (w <= 0 || h <= 0) return NULL;
    /* Limite de seguridad para evitar desbordamientos en imagenes corruptas */
    if ((double)w * (double)h > 100000000.0) return NULL;

    int bytes_por_pixel = bits_pixel / 8;
    size_t tamano_fila = (size_t)((((long long)w * bytes_por_pixel + 3) / 4) * 4);
    size_t requerido = tamano_fila * (size_t)h;
    if (tamano_disponible < requerido) {
        printf("[ERROR] Datos de pixeles BMP truncados.\n");
        return NULL;
    }

    unsigned char *pixeles_grises = malloc((size_t)w * (size_t)h);
    if (!pixeles_grises) {
        printf("[ERROR] Memoria insuficiente para pixeles.\n");
        return NULL;
    }

    for (int i = 0; i < h; i++) {
        int y = arriba_abajo ? i : (h - i - 1);
        const unsigned char *fila = datos_pixeles + (size_t)i * tamano_fila;

        for (int x = 0; x < w; x++) {
            size_t idx = (size_t)x * (size_t)bytes_por_pixel;
            unsigned char b = fila[idx];
            unsigned char g = fila[idx + 1];
            unsigned char r = fila[idx + 2];
            pixeles_grises[(size_t)y * (size_t)w + (size_t)x] =
                (unsigned char)(0.299f * r + 0.587f * g + 0.114f * b);
        }
    }

    return pixeles_grises;
}

static int validar_encabezados_bmp(const archivoBMP *archivo, const informacionBMP *info,
                                   int *ancho, int *alto, int *arriba_abajo) {
    if (archivo->tipo != 0x4D42) {
        printf("[ERROR] El archivo no tiene firma BMP valida.\n");
        return 0;
    }
    if (info->compresion != 0) {
        printf("[ERROR] Solo se soportan BMP sin compresion (BI_RGB).\n");
        return 0;
    }
    if (info->bits_pixel != 24 && info->bits_pixel != 32) {
        printf("[ERROR] Solo se soportan BMP de 24 o 32 bits (recibido: %d bits).\n", info->bits_pixel);
        return 0;
    }
    if (info->ancho <= 0 || info->alto == 0) {
        printf("[ERROR] Dimensiones BMP invalidas (%dx%d).\n", info->ancho, info->alto);
        return 0;
    }

    *ancho = info->ancho;
    if (info->alto < 0) {
        *alto = -info->alto;
        *arriba_abajo = 1;
    } else {
        *alto = info->alto;
        *arriba_abajo = 0;
    }
    return 1;
}

unsigned char *cargar_bmp_grises_mem(const unsigned char *buffer, size_t tamano_buffer, int *ancho, int *alto) {
    if (!buffer || !ancho || !alto ||
        tamano_buffer < sizeof(archivoBMP) + sizeof(informacionBMP)) {
        return NULL;
    }

    archivoBMP archivo;
    informacionBMP info;
    memcpy(&archivo, buffer, sizeof(archivoBMP));
    memcpy(&info, buffer + sizeof(archivoBMP), sizeof(informacionBMP));

    int arriba_abajo = 0;
    if (!validar_encabezados_bmp(&archivo, &info, ancho, alto, &arriba_abajo)) {
        return NULL;
    }
    if (archivo.offset >= tamano_buffer) {
        printf("[ERROR] Offset BMP fuera de limites.\n");
        return NULL;
    }

    return convertir_filas_a_grises(buffer + archivo.offset, tamano_buffer - archivo.offset,
                                    *ancho, *alto, arriba_abajo, info.bits_pixel);
}

unsigned char *cargar_bmp_grises(const char *ruta, int *ancho, int *alto) {
    if (!ruta || !ancho || !alto) return NULL;

    FILE *f = fopen(ruta, "rb");
    if (!f) {
        printf("[ERROR] No se pudo abrir el archivo BMP: %s\n", ruta);
        return NULL;
    }

    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    long tamano = ftell(f);
    if (tamano <= 0 || (size_t)tamano < sizeof(archivoBMP) + sizeof(informacionBMP)) {
        printf("[ERROR] Archivo BMP invalido o vacio: %s\n", ruta);
        fclose(f);
        return NULL;
    }
    rewind(f);

    unsigned char *buffer = malloc((size_t)tamano);
    if (!buffer) {
        fclose(f);
        return NULL;
    }

    size_t leidos = fread(buffer, 1, (size_t)tamano, f);
    fclose(f);

    if (leidos != (size_t)tamano) {
        free(buffer);
        return NULL;
    }

    unsigned char *grises = cargar_bmp_grises_mem(buffer, leidos, ancho, alto);
    free(buffer);
    return grises;
}

static float muestrear_box_float(const float *src, int w, int h, int dst_x, int dst_y,
                                 int dst_w, int dst_h) {
    if (dst_w >= w || dst_h >= h) {
        /* Respaldo bilineal cuando se amplia o iguala alguna dimension */
        double fx = ((dst_x + 0.5) * (double)w / (double)dst_w) - 0.5;
        double fy = ((dst_y + 0.5) * (double)h / (double)dst_h) - 0.5;
        if (fx < 0.0) fx = 0.0;
        if (fy < 0.0) fy = 0.0;
        int x0 = (int)floor(fx), y0 = (int)floor(fy);
        int x1 = (x0 + 1 < w) ? (x0 + 1) : (w - 1);
        int y1 = (y0 + 1 < h) ? (y0 + 1) : (h - 1);
        double tx = fx - x0, ty = fy - y0;
        double top = src[(size_t)y0 * w + x0] * (1.0 - tx) + src[(size_t)y0 * w + x1] * tx;
        double bot = src[(size_t)y1 * w + x0] * (1.0 - tx) + src[(size_t)y1 * w + x1] * tx;
        return (float)(top * (1.0 - ty) + bot * ty);
    }

    double sy0 = (double)dst_y * (double)h / (double)dst_h;
    double sy1 = (double)(dst_y + 1) * (double)h / (double)dst_h;
    double sx0 = (double)dst_x * (double)w / (double)dst_w;
    double sx1 = (double)(dst_x + 1) * (double)w / (double)dst_w;

    int iy0 = (int)floor(sy0);
    int iy1 = (int)ceil(sy1) - 1;
    int ix0 = (int)floor(sx0);
    int ix1 = (int)ceil(sx1) - 1;
    if (iy0 < 0) iy0 = 0;
    if (ix0 < 0) ix0 = 0;
    if (iy1 >= h) iy1 = h - 1;
    if (ix1 >= w) ix1 = w - 1;

    double suma = 0.0, peso_total = 0.0;
    for (int iy = iy0; iy <= iy1; iy++) {
        double y_lo = (sy0 > (double)iy) ? sy0 : (double)iy;
        double y_hi = (sy1 < (double)(iy + 1)) ? sy1 : (double)(iy + 1);
        double wy = y_hi - y_lo;
        if (wy <= 0.0) continue;

        const float *fila = src + (size_t)iy * (size_t)w;
        for (int ix = ix0; ix <= ix1; ix++) {
            double x_lo = (sx0 > (double)ix) ? sx0 : (double)ix;
            double x_hi = (sx1 < (double)(ix + 1)) ? sx1 : (double)(ix + 1);
            double wx = x_hi - x_lo;
            if (wx <= 0.0) continue;

            double peso = wy * wx;
            suma += peso * fila[ix];
            peso_total += peso;
        }
    }
    return (peso_total > 0.0) ? (float)(suma / peso_total) : 0.0f;
}

unsigned char *redimensionar_grises(const unsigned char *pixeles, int ancho_orig, int alto_orig,
                                    int nuevo_ancho, int nuevo_alto) {
    if (!pixeles || ancho_orig <= 0 || alto_orig <= 0 || nuevo_ancho <= 0 || nuevo_alto <= 0) {
        return NULL;
    }

    size_t total_orig = (size_t)ancho_orig * (size_t)alto_orig;
    float *src_f = malloc(total_orig * sizeof(float));
    unsigned char *imagen_redi = malloc((size_t)nuevo_ancho * (size_t)nuevo_alto);
    if (!src_f || !imagen_redi) {
        free(src_f);
        free(imagen_redi);
        return NULL;
    }

    for (size_t i = 0; i < total_orig; i++) {
        src_f[i] = (float)pixeles[i];
    }

    for (int y = 0; y < nuevo_alto; y++) {
        for (int x = 0; x < nuevo_ancho; x++) {
            int val = (int)(muestrear_box_float(src_f, ancho_orig, alto_orig, x, y,
                                                 nuevo_ancho, nuevo_alto) + 0.5f);
            if (val < 0) val = 0;
            if (val > 255) val = 255;
            imagen_redi[(size_t)y * (size_t)nuevo_ancho + (size_t)x] = (unsigned char)val;
        }
    }

    free(src_f);
    return imagen_redi;
}

int extraer_entradas_ecg(const unsigned char *pixeles, int ancho_orig, int alto_orig,
                         int lado, float *entradas, unsigned char *preview_out) {
    if (!pixeles || !entradas || ancho_orig <= 0 || alto_orig <= 0 || lado <= 0) {
        return 0;
    }

    size_t total = (size_t)ancho_orig * (size_t)alto_orig;
    unsigned int hist[256] = {0};
    double suma = 0.0;
    for (size_t i = 0; i < total; i++) {
        unsigned char p = pixeles[i];
        hist[p]++;
        suma += p;
    }

    double media = suma / (double)total;
    int mediana = 255;
    size_t acum = 0;
    size_t mitad = total / 2;
    for (int v = 0; v < 256; v++) {
        acum += hist[v];
        if (acum >= mitad) {
            mediana = v;
            break;
        }
    }

    float *tinta = malloc(total * sizeof(float));
    if (!tinta) return 0;

    /* Eliminar fondo de papel/cuadricula tenue preservando el trazo oscuro del ECG */
    if (media > 127.5) {
        float papel = (mediana >= 245) ? 210.0f : ((float)mediana * 0.83f);
        if (papel < 80.0f) papel = 80.0f;
        if (papel > 215.0f) papel = 215.0f;
        for (size_t i = 0; i < total; i++) {
            float p = (float)pixeles[i];
            tinta[i] = (p < papel) ? ((papel - p) / papel) : 0.0f;
        }
    } else {
        float fondo = (mediana <= 10) ? 45.0f : (255.0f - (255.0f - (float)mediana) * 0.83f);
        if (fondo < 40.0f) fondo = 40.0f;
        if (fondo > 175.0f) fondo = 175.0f;
        float rango = 255.0f - fondo;
        for (size_t i = 0; i < total; i++) {
            float p = (float)pixeles[i];
            if (p > fondo) {
                float v = (p - fondo) / rango;
                tinta[i] = (v > 1.0f) ? 1.0f : v;
            } else {
                tinta[i] = 0.0f;
            }
        }
    }

    for (int y = 0; y < lado; y++) {
        for (int x = 0; x < lado; x++) {
            float v = muestrear_box_float(tinta, ancho_orig, alto_orig, x, y, lado, lado);
            if (v < 0.0f) v = 0.0f;
            if (v > 1.0f) v = 1.0f;
            size_t idx = (size_t)y * (size_t)lado + (size_t)x;
            entradas[idx] = v;
            if (preview_out) {
                int pv = (int)(v * 255.0f + 0.5f);
                if (pv < 0) pv = 0;
                if (pv > 255) pv = 255;
                preview_out[idx] = (unsigned char)pv;
            }
        }
    }

    free(tinta);
    return 1;
}

void preparar_entradas_bmp(const unsigned char *pixeles, float *entradas, int n) {
    if (!pixeles || !entradas || n <= 0) return;

    double suma = 0.0;
    for (int i = 0; i < n; i++) {
        suma += pixeles[i] / 255.0;
    }
    int invertir = (suma / (double)n) > 0.5;

    for (int i = 0; i < n; i++) {
        float val = pixeles[i] / 255.0f;
        entradas[i] = invertir ? (1.0f - val) : val;
    }
}

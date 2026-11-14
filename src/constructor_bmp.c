#include "constructor_bmp.h"
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

unsigned char *redimensionar_grises(const unsigned char *pixeles, int ancho_orig, int alto_orig,
                                    int nuevo_ancho, int nuevo_alto) {
    if (!pixeles || ancho_orig <= 0 || alto_orig <= 0 || nuevo_ancho <= 0 || nuevo_alto <= 0) {
        return NULL;
    }

    unsigned char *imagen_redi = malloc((size_t)nuevo_ancho * (size_t)nuevo_alto);
    if (!imagen_redi) return NULL;

    for (int y = 0; y < nuevo_alto; y++) {
        int orig_y = (int)(((double)y + 0.5) * (double)alto_orig / (double)nuevo_alto);
        if (orig_y >= alto_orig) orig_y = alto_orig - 1;
        for (int x = 0; x < nuevo_ancho; x++) {
            int orig_x = (int)(((double)x + 0.5) * (double)ancho_orig / (double)nuevo_ancho);
            if (orig_x >= ancho_orig) orig_x = ancho_orig - 1;
            imagen_redi[(size_t)y * (size_t)nuevo_ancho + (size_t)x] =
                pixeles[(size_t)orig_y * (size_t)ancho_orig + (size_t)orig_x];
        }
    }
    return imagen_redi;
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

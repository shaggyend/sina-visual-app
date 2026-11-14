#ifndef CONSTRUCTOR_BMP_H
#define CONSTRUCTOR_BMP_H

#include <stddef.h>

#pragma pack(push, 1)
typedef struct {
    unsigned short tipo;      /* "BM" (0x4D42) */
    unsigned int tamano;
    unsigned short reserved1;
    unsigned short reserved2;
    unsigned int offset;
} archivoBMP;

typedef struct {
    unsigned int tamano_enca;
    int ancho;
    int alto;
    unsigned short planos;
    unsigned short bits_pixel;
    unsigned int compresion;
    unsigned int tamano;
    int resolucion_x;
    int resolucion_y;
    unsigned int relleno1;
    unsigned int relleno2;
} informacionBMP;
#pragma pack(pop)

/* Carga BMP de 24/32 bits desde archivo a escala de grises (0-255). */
unsigned char *cargar_bmp_grises(const char *ruta, int *ancho, int *alto);

/* Carga BMP de 24/32 bits desde un buffer en memoria a escala de grises (0-255). */
unsigned char *cargar_bmp_grises_mem(const unsigned char *buffer, size_t tamano_buffer, int *ancho, int *alto);

/* Redimensiona buffer de grises por vecino mas cercano. */
unsigned char *redimensionar_grises(const unsigned char *pixeles, int ancho_orig, int alto_orig,
                                    int nuevo_ancho, int nuevo_alto);

/* Normaliza pixeles [0..255] a [0..1], invirtiendo automaticamente fondo blanco de papel ECG. */
void preparar_entradas_bmp(const unsigned char *pixeles, float *entradas, int n);

#endif

#ifndef CONFIG_H
#define CONFIG_H

#include <stdio.h>

#define MAX_CAPAS_CONFIG 10

typedef struct {
    int topologia_bmp[MAX_CAPAS_CONFIG];
    int num_capas_bmp;
    char dataset_bmp[256];
    char pesos_bmp[256];
    float tasa_bmp;
    int epocas_bmp;

    int topologia_manual[MAX_CAPAS_CONFIG];
    int num_capas_manual;
    char dataset_manual[256];
    char pesos_manual[256];
    float tasa_manual;
    int epocas_manual;

    int epocas_general;
    int puerto_servidor;
    char admin_pin[64];
} Configuracion;

/* Carga configuracion desde archivo con valores por defecto seguros. */
Configuracion cargar_config(const char *archivo);

/* Abre un archivo probando ruta directa y '../ruta' (ejecucion desde output/). */
FILE *abrir_archivo_relativo(const char *ruta, const char *modo, char *ruta_resuelta, int tam_ruta);

#endif

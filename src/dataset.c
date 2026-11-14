#include "dataset.h"
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DATASET_BUFFER 66000

void normalizar_variables_clinicas(const float *brutos, float *normalizados) {
    normalizados[0] = brutos[0] / 1500.0f; /* RR */
    normalizados[1] = brutos[1] / 300.0f;  /* PR */
    normalizados[2] = brutos[2] / 200.0f;  /* QRS */
    normalizados[3] = brutos[3] / 600.0f;  /* QT */
    normalizados[4] = brutos[4] / 200.0f;  /* BPM */
}

static const char *saltar_bom_utf8(const char *linea) {
    const unsigned char *u = (const unsigned char *)linea;
    if (u[0] == 0xEF && u[1] == 0xBB && u[2] == 0xBF) {
        return linea + 3;
    }
    return linea;
}

static int es_linea_valida(const char *linea) {
    if (!linea) return 0;
    const char *p = saltar_bom_utf8(linea);
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '\0' || *p == '\n' || *p == '\r' || *p == '#') return 0;
    return strlen(p) > 5;
}

datos *cargar_dataset(const char *ruta_archivo, int num_pixeles, int num_salidas) {
    FILE *data = abrir_archivo_relativo(ruta_archivo, "r", NULL, 0);
    if (!data) {
        printf("[ERROR] No se pudo abrir el dataset: %s\n", ruta_archivo);
        return NULL;
    }

    char *buffer = malloc(DATASET_BUFFER);
    if (!buffer) {
        printf("[ERROR] Sin memoria suficiente para el buffer de lectura.\n");
        fclose(data);
        return NULL;
    }

    int ejemplos = 0;
    while (fgets(buffer, DATASET_BUFFER, data)) {
        if (es_linea_valida(buffer)) ejemplos++;
    }

    if (ejemplos == 0) {
        printf("[ERROR] El dataset esta vacio: %s\n", ruta_archivo);
        free(buffer);
        fclose(data);
        return NULL;
    }

    datos *dataset = malloc(sizeof(datos));
    if (!dataset) {
        free(buffer);
        fclose(data);
        return NULL;
    }

    dataset->ejemplos = ejemplos;
    dataset->num_pixeles = num_pixeles;
    dataset->num_salida = num_salidas;
    dataset->entradas = calloc((size_t)ejemplos, sizeof(float *));
    dataset->salidas = calloc((size_t)ejemplos, sizeof(float *));
    if (!dataset->entradas || !dataset->salidas) {
        liberarDatos(dataset);
        free(buffer);
        fclose(data);
        return NULL;
    }

    rewind(data);

    int idx = 0;
    while (fgets(buffer, DATASET_BUFFER, data) && idx < ejemplos) {
        if (!es_linea_valida(buffer)) continue;

        dataset->entradas[idx] = calloc((size_t)num_pixeles, sizeof(float));
        dataset->salidas[idx] = calloc((size_t)num_salidas, sizeof(float));
        if (!dataset->entradas[idx] || !dataset->salidas[idx]) {
            dataset->ejemplos = idx + 1;
            liberarDatos(dataset);
            free(buffer);
            fclose(data);
            return NULL;
        }

        char *linea_limpia = (char *)saltar_bom_utf8(buffer);
        char *token = strtok(linea_limpia, ",\r\n");

        for (int j = 0; j < num_pixeles && token != NULL; j++) {
            dataset->entradas[idx][j] = (float)atof(token);
            token = strtok(NULL, ",\r\n");
        }
        for (int j = 0; j < num_salidas && token != NULL; j++) {
            dataset->salidas[idx][j] = (float)atof(token);
            token = strtok(NULL, ",\r\n");
        }

        if (num_pixeles == 5) {
            float temp[5];
            memcpy(temp, dataset->entradas[idx], sizeof(temp));
            normalizar_variables_clinicas(temp, dataset->entradas[idx]);
        }

        idx++;
    }

    dataset->ejemplos = idx;
    free(buffer);
    fclose(data);
    return dataset;
}

void liberarDatos(datos *datos_ptr) {
    if (!datos_ptr) return;
    if (datos_ptr->entradas && datos_ptr->salidas) {
        for (int i = 0; i < datos_ptr->ejemplos; i++) {
            free(datos_ptr->entradas[i]);
            free(datos_ptr->salidas[i]);
        }
    }
    free(datos_ptr->entradas);
    free(datos_ptr->salidas);
    free(datos_ptr);
}

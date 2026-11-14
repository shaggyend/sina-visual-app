#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "config.h"

static void recortar_linea(char *s) {
    if (!s) return;
    size_t len = strlen(s);
    while (len > 0 && (s[len - 1] == '\n' || s[len - 1] == '\r' || s[len - 1] == ' ' || s[len - 1] == '\t')) {
        s[--len] = '\0';
    }
}

FILE *abrir_archivo_relativo(const char *ruta, const char *modo, char *ruta_resuelta, int tam_ruta) {
    if (!ruta || !modo) return NULL;
    FILE *f = fopen(ruta, modo);
    if (f) {
        if (ruta_resuelta && tam_ruta > 0) snprintf(ruta_resuelta, tam_ruta, "%s", ruta);
        return f;
    }

    char alt[512];
    snprintf(alt, sizeof(alt), "../%s", ruta);
    f = fopen(alt, modo);
    if (f && ruta_resuelta && tam_ruta > 0) {
        snprintf(ruta_resuelta, tam_ruta, "%s", alt);
    }
    return f;
}

static int parsear_topologia(const char *valor, int *destino, int max_capas) {
    char copia[256];
    snprintf(copia, sizeof(copia), "%s", valor);
    recortar_linea(copia);

    int count = 0;
    char *tok = strtok(copia, ",");
    while (tok && count < max_capas) {
        int val = atoi(tok);
        if (val > 0) destino[count++] = val;
        tok = strtok(NULL, ",");
    }
    return count;
}

Configuracion cargar_config(const char *archivo) {
    Configuracion cfg;
    memset(&cfg, 0, sizeof(cfg));

    cfg.num_capas_bmp = 3;
    cfg.topologia_bmp[0] = 1024;
    cfg.topologia_bmp[1] = 64;
    cfg.topologia_bmp[2] = 4;
    snprintf(cfg.dataset_bmp, sizeof(cfg.dataset_bmp), "data/dataset_bmp.csv");
    snprintf(cfg.pesos_bmp, sizeof(cfg.pesos_bmp), "data/pesos_bmp.nn");
    cfg.tasa_bmp = 0.015f;
    cfg.epocas_bmp = 120;

    cfg.num_capas_manual = 3;
    cfg.topologia_manual[0] = 5;
    cfg.topologia_manual[1] = 12;
    cfg.topologia_manual[2] = 4;
    snprintf(cfg.dataset_manual, sizeof(cfg.dataset_manual), "data/dataset_manual.csv");
    snprintf(cfg.pesos_manual, sizeof(cfg.pesos_manual), "data/pesos_manual.nn");
    cfg.tasa_manual = 0.08f;
    cfg.epocas_manual = 400;

    cfg.epocas_general = 120;
    cfg.puerto_servidor = 8080;
    snprintf(cfg.admin_pin, sizeof(cfg.admin_pin), "admin123");

    FILE *f = abrir_archivo_relativo(archivo, "r", NULL, 0);
    if (!f) return cfg;

    char linea[512];
    int tiene_epocas_bmp = 0;
    int tiene_epocas_manual = 0;
    while (fgets(linea, sizeof(linea), f)) {
        recortar_linea(linea);
        if (linea[0] == '\0' || linea[0] == '#') continue;

        char *eq = strchr(linea, '=');
        if (!eq) continue;
        *eq = '\0';
        const char *clave = linea;
        const char *valor = eq + 1;

        if (strcmp(clave, "TOPOLOGIA_BMP") == 0) {
            int n = parsear_topologia(valor, cfg.topologia_bmp, MAX_CAPAS_CONFIG);
            if (n >= 2) cfg.num_capas_bmp = n;
        } else if (strcmp(clave, "TOPOLOGIA_MANUAL") == 0) {
            int n = parsear_topologia(valor, cfg.topologia_manual, MAX_CAPAS_CONFIG);
            if (n >= 2) cfg.num_capas_manual = n;
        } else if (strcmp(clave, "DATASET_BMP") == 0 && valor[0] != '\0') {
            snprintf(cfg.dataset_bmp, sizeof(cfg.dataset_bmp), "%s", valor);
        } else if (strcmp(clave, "DATASET_MANUAL") == 0 && valor[0] != '\0') {
            snprintf(cfg.dataset_manual, sizeof(cfg.dataset_manual), "%s", valor);
        } else if (strcmp(clave, "PESOS_BMP") == 0 && valor[0] != '\0') {
            snprintf(cfg.pesos_bmp, sizeof(cfg.pesos_bmp), "%s", valor);
        } else if (strcmp(clave, "PESOS_MANUAL") == 0 && valor[0] != '\0') {
            snprintf(cfg.pesos_manual, sizeof(cfg.pesos_manual), "%s", valor);
        } else if (strcmp(clave, "TASA_BMP") == 0) {
            float t = (float)atof(valor);
            if (t > 0.0f) cfg.tasa_bmp = t;
        } else if (strcmp(clave, "TASA_MANUAL") == 0) {
            float t = (float)atof(valor);
            if (t > 0.0f) cfg.tasa_manual = t;
        } else if (strcmp(clave, "EPOCAS_GENERAL") == 0) {
            int e = atoi(valor);
            if (e > 0) {
                cfg.epocas_general = e;
                if (!tiene_epocas_bmp) cfg.epocas_bmp = e;
                if (!tiene_epocas_manual) cfg.epocas_manual = e;
            }
        } else if (strcmp(clave, "EPOCAS_BMP") == 0) {
            int e = atoi(valor);
            if (e > 0) {
                cfg.epocas_bmp = e;
                tiene_epocas_bmp = 1;
            }
        } else if (strcmp(clave, "EPOCAS_MANUAL") == 0) {
            int e = atoi(valor);
            if (e > 0) {
                cfg.epocas_manual = e;
                tiene_epocas_manual = 1;
            }
        } else if (strcmp(clave, "PUERTO_SERVIDOR") == 0) {
            int p = atoi(valor);
            if (p > 0 && p <= 65535) cfg.puerto_servidor = p;
        } else if (strcmp(clave, "ADMIN_PIN") == 0 && valor[0] != '\0') {
            snprintf(cfg.admin_pin, sizeof(cfg.admin_pin), "%s", valor);
        }
    }

    fclose(f);
    return cfg;
}

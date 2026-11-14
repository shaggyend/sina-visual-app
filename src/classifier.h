#ifndef CLASSIFIER_H
#define CLASSIFIER_H

#include "neural_network.h"

#define TIPO_NORMAL      0
#define TIPO_ARRITMIA    1
#define TIPO_INFARTO     2
#define TIPO_HISTORIAL   3
#define NUM_CLASES_ECG   4

#define URGENCIA_NINGUNA 0
#define URGENCIA_MEDIA   1
#define URGENCIA_ALTA    2

typedef struct {
    int hay_arritmia;
    int tipo_diagnostico;
    float confianza;
    float probabilidades[NUM_CLASES_ECG];
    int nivel_urgencia;
    char descripcion[64];
    char recomendacion[128];
} ResultadoECG;

typedef struct {
    char nombre[100];
    char cedula[20];
    char medico[100];
    char fecha[20];
} Paciente;

typedef struct {
    Paciente paciente;
    int usa_bmp;
    float intervalos_manuales[5];
    ResultadoECG resultado;
    int verificado;
    int descartado;
    int diagnostico_corregido;
    int tipo_correcto;
} Caso;

ResultadoECG clasificar(redNeuronal *red, float *entradas);
int calcular_urgencia(int tipo_diagnostico, float confianza);
const char *obtener_descripcion(int tipo);
const char *obtener_recomendacion(int urgencia);
const char *obtener_texto_urgencia(int urgencia);

#endif

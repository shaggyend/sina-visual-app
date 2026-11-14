#ifndef CASOS_H
#define CASOS_H

#include "classifier.h"

#define ARCHIVO_CASOS_DEFECTO "reports/casos.csv"

typedef struct {
    int total;
    int por_tipo[NUM_CLASES_ECG];
    int verificados;
    int pendientes;
    int descartados;
    int corregidos;
} EstadisticasCasos;

/* Guarda un caso en reports/casos.csv y genera/agrega el reporte clinico .txt */
int guardar_caso_clinico(const Caso *caso);

/* Carga todos los casos registrados en memoria (liberar con free). */
int cargar_todos_casos(Caso **lista_out, int *cantidad_out);

/* Actualiza el estado de verificacion de un caso por indice (0-based) preservando todas las columnas. */
int actualizar_estado_caso(int indice, int verificado, int descartado, int corregido, int tipo_correcto);

/* Calcula estadisticas agregadas de los casos registrados. */
EstadisticasCasos calcular_estadisticas_casos(void);

/* Lee el contenido completo del reporte clinico por cedula en un buffer dinamico (liberar con free). */
char *leer_reporte_paciente(const char *cedula);

#endif

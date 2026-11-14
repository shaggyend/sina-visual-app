#ifndef DATASET_H
#define DATASET_H

/* Estructura en memoria de un dataset supervisado */
typedef struct {
    int ejemplos;
    int num_pixeles;
    int num_salida;
    float **entradas;
    float **salidas;
} datos;

/* Normaliza variables clinicas brutas (RR, PR, QRS, QT, BPM) al rango [0..1+] */
void normalizar_variables_clinicas(const float *brutos, float *normalizados);

/* Carga CSV de entrenamiento saltando BOM UTF-8, comentarios y lineas vacias */
datos *cargar_dataset(const char *ruta_archivo, int num_pixeles, int num_salidas);

/* Libera toda la memoria asignada al dataset */
void liberarDatos(datos *datos_ptr);

#endif

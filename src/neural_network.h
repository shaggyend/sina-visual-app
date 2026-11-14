#ifndef NEURAL_NETWORK_H
#define NEURAL_NETWORK_H

/* Capa de la red neuronal multicapa */
typedef struct {
    int cantidad_neuronas;
    int cantidad_entradas;
    float *z;             /* entrada * w + bias */
    float *activaciones;  /* salida despues de activacion */
    float *gradiente;     /* gradiente de error para backprop */
    float *pesos;         /* matriz [neurona * entradas] */
    float *bias;          /* vector [neurona] */
    float alpha;          /* parametro alpha de PReLU */
} capaRedNeurona;

/* Red neuronal completa */
typedef struct {
    int nume_capas;       /* capas con pesos (sin contar entrada) */
    int *topologia;       /* neuronas por cada capa [0..nume_capas] */
    capaRedNeurona *capa;
    float tasa_aprendizaje;
    int epocas;
    int entrenada;        /* 1 si fue entrenada o cargo pesos validos */
} redNeuronal;

/* Resumen estadistico del entrenamiento */
typedef struct {
    int epocas_completadas;
    float error_inicial;
    float error_final;
    float precision_final;
} EntrenamientoResumen;

/* Funciones matematicas */
float PRelu(float z, float alpha);
float derivada_PRelu(float z, float alpha);
float sigmoide(float z);
float loss(float prediccion, float real);

/* Ciclo de vida y operaciones de la red */
redNeuronal *inicializar_red(int num_capas, int *topologia, float tasa_aprendizaje, int epocas);
void reinicializar_pesos(redNeuronal *red);
void forwardPropagation(redNeuronal *red, float *entradas);
void backPropagation(redNeuronal *red, float *salidas_esperadas);
void entrenar_red(redNeuronal *red, float **entradas, float **salidas, int ejemplos, int num_salidas);
int entrenar_red_con_log(redNeuronal *red, float **entradas, float **salidas, int ejemplos,
                         int num_salidas, const char *ruta_log, EntrenamientoResumen *resumen);
void liberar_red(redNeuronal *red);

/* Persistencia de pesos entrenados */
int guardar_pesos(const redNeuronal *red, const char *ruta);
int cargar_pesos(redNeuronal *red, const char *ruta);

#endif

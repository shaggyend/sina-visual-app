#include "neural_network.h"
#include "config.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

float PRelu(float z, float alpha) {
    return (z > 0.0f) ? z : (alpha * z);
}

float derivada_PRelu(float z, float alpha) {
    return (z > 0.0f) ? 1.0f : alpha;
}

float sigmoide(float z) {
    if (z > 20.0f) return 1.0f / (1.0f + expf(-20.0f));
    if (z < -20.0f) return 1.0f / (1.0f + expf(20.0f));
    return 1.0f / (1.0f + expf(-z));
}

float loss(float prediccion, float real) {
    const float epsilon = 1e-7f;
    if (prediccion < epsilon) prediccion = epsilon;
    if (prediccion > 1.0f - epsilon) prediccion = 1.0f - epsilon;
    return -(real * logf(prediccion) + (1.0f - real) * logf(1.0f - prediccion));
}

static float random_float(void) {
    return ((float)rand() / (float)RAND_MAX) * 2.0f - 1.0f;
}

static int argmax_vec(const float *v, int n) {
    int idx = 0;
    float best = v[0];
    for (int i = 1; i < n; i++) {
        if (v[i] > best) {
            best = v[i];
            idx = i;
        }
    }
    return idx;
}

redNeuronal *inicializar_red(int num_capas, int *topologia, float tasa_aprendizaje, int epocas) {
    if (num_capas < 2 || !topologia) return NULL;

    redNeuronal *red = malloc(sizeof(redNeuronal));
    if (!red) return NULL;

    red->tasa_aprendizaje = tasa_aprendizaje;
    red->epocas = epocas;
    red->nume_capas = num_capas - 1;
    red->entrenada = 0;

    red->topologia = malloc((size_t)num_capas * sizeof(int));
    red->capa = calloc((size_t)red->nume_capas, sizeof(capaRedNeurona));
    if (!red->topologia || !red->capa) {
        free(red->topologia);
        free(red->capa);
        free(red);
        return NULL;
    }

    for (int i = 0; i < num_capas; i++) {
        red->topologia[i] = topologia[i];
    }

    static int semilla_inicializada = 0;
    if (!semilla_inicializada) {
        srand((unsigned int)time(NULL));
        semilla_inicializada = 1;
    }

    for (int i = 0; i < red->nume_capas; i++) {
        int entradas = red->topologia[i];
        int neuronas = red->topologia[i + 1];

        red->capa[i].cantidad_entradas = entradas;
        red->capa[i].cantidad_neuronas = neuronas;
        red->capa[i].alpha = 0.01f;

        red->capa[i].z = calloc((size_t)neuronas, sizeof(float));
        red->capa[i].activaciones = calloc((size_t)neuronas, sizeof(float));
        red->capa[i].gradiente = calloc((size_t)neuronas, sizeof(float));
        red->capa[i].pesos = malloc((size_t)neuronas * (size_t)entradas * sizeof(float));
        red->capa[i].bias = malloc((size_t)neuronas * sizeof(float));

        if (!red->capa[i].z || !red->capa[i].activaciones || !red->capa[i].gradiente ||
            !red->capa[i].pesos || !red->capa[i].bias) {
            liberar_red(red);
            return NULL;
        }
    }

    reinicializar_pesos(red);
    return red;
}

void reinicializar_pesos(redNeuronal *red) {
    if (!red || !red->capa) return;
    for (int i = 0; i < red->nume_capas; i++) {
        capaRedNeurona *c = &red->capa[i];
        int entradas = c->cantidad_entradas;
        int neuronas = c->cantidad_neuronas;
        float escala = sqrtf(2.0f / (float)entradas);

        c->alpha = 0.01f;
        for (int j = 0; j < neuronas; j++) {
            c->bias[j] = random_float() * 0.05f;
            for (int k = 0; k < entradas; k++) {
                c->pesos[j * entradas + k] = random_float() * escala;
            }
        }
    }
    red->entrenada = 0;
}

void forwardPropagation(redNeuronal *red, float *entradas) {
    if (!red || !entradas) return;
    float *input_actual = entradas;

    for (int i = 0; i < red->nume_capas; i++) {
        capaRedNeurona *c = &red->capa[i];
        int es_salida = (i == red->nume_capas - 1);

        for (int j = 0; j < c->cantidad_neuronas; j++) {
            float suma = c->bias[j];
            const float *fila_pesos = &c->pesos[j * c->cantidad_entradas];
            for (int k = 0; k < c->cantidad_entradas; k++) {
                suma += input_actual[k] * fila_pesos[k];
            }
            c->z[j] = suma;
            c->activaciones[j] = es_salida ? sigmoide(suma) : PRelu(suma, c->alpha);
        }
        input_actual = c->activaciones;
    }
}

void backPropagation(redNeuronal *red, float *salidas_esperadas) {
    if (!red || !salidas_esperadas) return;

    int ultima_capa_idx = red->nume_capas - 1;
    capaRedNeurona *capa_salida = &red->capa[ultima_capa_idx];

    for (int j = 0; j < capa_salida->cantidad_neuronas; j++) {
        capa_salida->gradiente[j] = capa_salida->activaciones[j] - salidas_esperadas[j];
    }

    for (int i = ultima_capa_idx - 1; i >= 0; i--) {
        capaRedNeurona *capa_actual = &red->capa[i];
        capaRedNeurona *capa_siguiente = &red->capa[i + 1];

        for (int j = 0; j < capa_actual->cantidad_neuronas; j++) {
            float suma_error = 0.0f;
            for (int k = 0; k < capa_siguiente->cantidad_neuronas; k++) {
                suma_error += capa_siguiente->gradiente[k] *
                              capa_siguiente->pesos[k * capa_siguiente->cantidad_entradas + j];
            }
            capa_actual->gradiente[j] = suma_error * derivada_PRelu(capa_actual->z[j], capa_actual->alpha);
        }
    }
}

static void actualizar_pesos(redNeuronal *red, float *entradas) {
    for (int i = 0; i < red->nume_capas; i++) {
        capaRedNeurona *c = &red->capa[i];
        float *input_actual = (i == 0) ? entradas : red->capa[i - 1].activaciones;

        for (int j = 0; j < c->cantidad_neuronas; j++) {
            float lr_grad = red->tasa_aprendizaje * c->gradiente[j];
            float *fila_pesos = &c->pesos[j * c->cantidad_entradas];
            for (int k = 0; k < c->cantidad_entradas; k++) {
                fila_pesos[k] -= lr_grad * input_actual[k];
            }
            c->bias[j] -= lr_grad;
        }
    }
}

int entrenar_red_con_log(redNeuronal *red, float **entradas, float **salidas, int ejemplos,
                         int num_salidas, const char *ruta_log, EntrenamientoResumen *resumen) {
    if (!red || !entradas || !salidas || ejemplos <= 0 || num_salidas <= 0) {
        return 0;
    }

    int *indices = malloc((size_t)ejemplos * sizeof(int));
    if (!indices) return 0;
    for (int i = 0; i < ejemplos; i++) indices[i] = i;

    FILE *log = NULL;
    if (ruta_log && ruta_log[0] != '\0') {
        log = abrir_archivo_relativo(ruta_log, "w", NULL, 0);
        if (log) {
            fprintf(log, "epoca,error,precision\n");
        }
    }

    float err_ini = 0.0f;
    float err_fin = 0.0f;
    float acc_fin = 0.0f;

    for (int e = 0; e < red->epocas; e++) {
        /* Barajar orden de ejemplos en cada epoca (Fisher-Yates) */
        for (int i = ejemplos - 1; i > 0; i--) {
            int j = rand() % (i + 1);
            int tmp = indices[i];
            indices[i] = indices[j];
            indices[j] = tmp;
        }

        float error_epoca = 0.0f;
        int aciertos = 0;

        for (int idx_pos = 0; idx_pos < ejemplos; idx_pos++) {
            int i = indices[idx_pos];
            forwardPropagation(red, entradas[i]);
            backPropagation(red, salidas[i]);
            actualizar_pesos(red, entradas[i]);

            capaRedNeurona *capa_salida = &red->capa[red->nume_capas - 1];
            for (int j = 0; j < num_salidas; j++) {
                error_epoca += loss(capa_salida->activaciones[j], salidas[i][j]);
            }

            if (argmax_vec(capa_salida->activaciones, num_salidas) == argmax_vec(salidas[i], num_salidas)) {
                aciertos++;
            }
        }

        error_epoca /= (float)(ejemplos * num_salidas);
        float precision = ((float)aciertos / (float)ejemplos) * 100.0f;

        if (e == 0) err_ini = error_epoca;
        err_fin = error_epoca;
        acc_fin = precision;

        if (log) {
            fprintf(log, "%d,%.6f,%.2f\n", e, error_epoca, precision);
        }
        if (e % 20 == 0 || e == red->epocas - 1) {
            printf("  Epoca %3d/%d | Loss: %.4f | Precision: %.1f%%\n",
                   e + 1, red->epocas, error_epoca, precision);
        }
    }

    if (log) fclose(log);
    free(indices);

    red->entrenada = 1;
    if (resumen) {
        resumen->epocas_completadas = red->epocas;
        resumen->error_inicial = err_ini;
        resumen->error_final = err_fin;
        resumen->precision_final = acc_fin;
    }
    return 1;
}

void entrenar_red(redNeuronal *red, float **entradas, float **salidas, int ejemplos, int num_salidas) {
    entrenar_red_con_log(red, entradas, salidas, ejemplos, num_salidas, "reports/training_log.txt", NULL);
}

int guardar_pesos(const redNeuronal *red, const char *ruta) {
    if (!red || !ruta) return 0;
    FILE *f = abrir_archivo_relativo(ruta, "w", NULL, 0);
    if (!f) return 0;

    fprintf(f, "SINA_NN_V1\n");
    fprintf(f, "%d\n", red->nume_capas);
    for (int i = 0; i <= red->nume_capas; i++) {
        fprintf(f, "%d%s", red->topologia[i], (i == red->nume_capas) ? "\n" : " ");
    }
    fprintf(f, "%.8f %d\n", red->tasa_aprendizaje, red->epocas);

    for (int i = 0; i < red->nume_capas; i++) {
        const capaRedNeurona *c = &red->capa[i];
        fprintf(f, "%.8f\n", c->alpha);
        for (int j = 0; j < c->cantidad_neuronas; j++) {
            fprintf(f, "%.8f%s", c->bias[j], (j == c->cantidad_neuronas - 1) ? "\n" : " ");
        }
        int total_pesos = c->cantidad_neuronas * c->cantidad_entradas;
        for (int p = 0; p < total_pesos; p++) {
            fprintf(f, "%.8f%s", c->pesos[p], ((p + 1) % c->cantidad_entradas == 0) ? "\n" : " ");
        }
    }

    fclose(f);
    return 1;
}

int cargar_pesos(redNeuronal *red, const char *ruta) {
    if (!red || !ruta) return 0;
    FILE *f = abrir_archivo_relativo(ruta, "r", NULL, 0);
    if (!f) return 0;

    char cabecera[32] = {0};
    if (fscanf(f, "%31s", cabecera) != 1 || strcmp(cabecera, "SINA_NN_V1") != 0) {
        fclose(f);
        return 0;
    }

    int num_capas = 0;
    if (fscanf(f, "%d", &num_capas) != 1 || num_capas != red->nume_capas) {
        fclose(f);
        return 0;
    }

    for (int i = 0; i <= num_capas; i++) {
        int tam = 0;
        if (fscanf(f, "%d", &tam) != 1 || tam != red->topologia[i]) {
            fclose(f);
            return 0;
        }
    }

    float tasa_archivo = 0.0f;
    int epocas_archivo = 0;
    if (fscanf(f, "%f %d", &tasa_archivo, &epocas_archivo) != 2) {
        fclose(f);
        return 0;
    }

    for (int i = 0; i < red->nume_capas; i++) {
        capaRedNeurona *c = &red->capa[i];
        if (fscanf(f, "%f", &c->alpha) != 1) {
            fclose(f);
            return 0;
        }
        for (int j = 0; j < c->cantidad_neuronas; j++) {
            if (fscanf(f, "%f", &c->bias[j]) != 1) {
                fclose(f);
                return 0;
            }
        }
        int total_pesos = c->cantidad_neuronas * c->cantidad_entradas;
        for (int p = 0; p < total_pesos; p++) {
            if (fscanf(f, "%f", &c->pesos[p]) != 1) {
                fclose(f);
                return 0;
            }
        }
    }

    fclose(f);
    red->entrenada = 1;
    return 1;
}

void liberar_red(redNeuronal *red) {
    if (!red) return;
    if (red->capa) {
        for (int i = 0; i < red->nume_capas; i++) {
            free(red->capa[i].z);
            free(red->capa[i].activaciones);
            free(red->capa[i].gradiente);
            free(red->capa[i].pesos);
            free(red->capa[i].bias);
        }
        free(red->capa);
    }
    free(red->topologia);
    free(red);
}

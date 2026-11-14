#include "casos.h"
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void sanitizar_csv(const char *src, char *dst, size_t tam) {
    size_t j = 0;
    for (size_t i = 0; src && src[i] != '\0' && j + 1 < tam; i++) {
        char c = src[i];
        if (c == ',' || c == '\n' || c == '\r') c = ' ';
        dst[j++] = c;
    }
    dst[j] = '\0';
}

static void asegurar_directorio_reports(void) {
    /* Si se ejecuta desde output/, ../reports existe; si desde raiz, reports existe. */
}

static void escribir_reporte_txt(const Caso *caso) {
    char ruta[160];
    snprintf(ruta, sizeof(ruta), "reports/%s.txt", caso->paciente.cedula);
    FILE *rep = abrir_archivo_relativo(ruta, "a", NULL, 0);
    if (!rep) return;

    fprintf(rep, "========================================\n");
    fprintf(rep, "  REPORTE CLINICO - SINA-VISUAL\n");
    fprintf(rep, "========================================\n");
    fprintf(rep, "  Paciente    : %s\n", caso->paciente.nombre);
    fprintf(rep, "  Cedula      : %s\n", caso->paciente.cedula);
    fprintf(rep, "  Medico      : %s\n", caso->paciente.medico);
    fprintf(rep, "  Fecha       : %s\n", caso->paciente.fecha);
    fprintf(rep, "----------------------------------------\n");

    if (caso->usa_bmp) {
        fprintf(rep, "  Modo        : Imagen BMP (Red 1024 px)\n");
    } else {
        fprintf(rep, "  Modo        : Variables Clinicas (5 entradas)\n");
        fprintf(rep, "  Valores     : RR=%.1f, PR=%.1f, QRS=%.1f, QT=%.1f, BPM=%.1f\n",
                caso->intervalos_manuales[0], caso->intervalos_manuales[1],
                caso->intervalos_manuales[2], caso->intervalos_manuales[3],
                caso->intervalos_manuales[4]);
    }

    fprintf(rep, "  Diagnostico IA : %s\n", obtener_descripcion(caso->resultado.tipo_diagnostico));
    fprintf(rep, "  Confianza      : %.1f%%\n", caso->resultado.confianza * 100.0f);
    fprintf(rep, "  Urgencia       : %s\n", obtener_texto_urgencia(caso->resultado.nivel_urgencia));
    fprintf(rep, "  Verificado     : %s\n", caso->verificado ? "SI" : "NO");
    if (caso->descartado) {
        fprintf(rep, "  Estado         : DESCARTADO (Ruido/Mala lectura)\n");
    } else if (caso->diagnostico_corregido) {
        fprintf(rep, "  Corregido a    : %s\n", obtener_descripcion(caso->tipo_correcto));
    }
    fprintf(rep, "========================================\n\n");
    fclose(rep);
}

int guardar_caso_clinico(const Caso *caso) {
    if (!caso) return 0;
    asegurar_directorio_reports();

    FILE *f = abrir_archivo_relativo(ARCHIVO_CASOS_DEFECTO, "a", NULL, 0);
    if (!f) return 0;

    char nombre[100], cedula[20], medico[100], fecha[20];
    sanitizar_csv(caso->paciente.nombre, nombre, sizeof(nombre));
    sanitizar_csv(caso->paciente.cedula, cedula, sizeof(cedula));
    sanitizar_csv(caso->paciente.medico, medico, sizeof(medico));
    sanitizar_csv(caso->paciente.fecha, fecha, sizeof(fecha));

    /* Esquema completo de 10 columnas sin perdida de datos */
    fprintf(f, "%s,%s,%s,%s,%d,%d,%d,%d,%d,%.4f,%d\n",
            nombre, cedula, medico, fecha,
            caso->resultado.tipo_diagnostico,
            caso->verificado,
            caso->descartado,
            caso->tipo_correcto,
            caso->diagnostico_corregido,
            caso->resultado.confianza,
            caso->usa_bmp);
    fclose(f);

    escribir_reporte_txt(caso);
    return 1;
}

static int parsear_linea_caso(const char *linea, Caso *c) {
    memset(c, 0, sizeof(Caso));
    int tipo = 0, verif = 0, desc = 0, tipo_corr = 0, diag_corr = 0, usa_bmp = 0;
    float conf = 0.0f;

    int campos = sscanf(linea, "%99[^,],%19[^,],%99[^,],%19[^,],%d,%d,%d,%d,%d,%f,%d",
                        c->paciente.nombre, c->paciente.cedula, c->paciente.medico, c->paciente.fecha,
                        &tipo, &verif, &desc, &tipo_corr, &diag_corr, &conf, &usa_bmp);
    if (campos < 7) return 0;

    if (campos < 8) tipo_corr = tipo;
    c->resultado.tipo_diagnostico = tipo;
    c->verificado = verif;
    c->descartado = desc;
    c->tipo_correcto = tipo_corr;
    c->diagnostico_corregido = diag_corr;
    c->resultado.confianza = conf;
    c->usa_bmp = usa_bmp;
    c->resultado.hay_arritmia = (tipo != TIPO_NORMAL);
    c->resultado.nivel_urgencia = calcular_urgencia(tipo, conf);
    snprintf(c->resultado.descripcion, sizeof(c->resultado.descripcion), "%s", obtener_descripcion(tipo));
    snprintf(c->resultado.recomendacion, sizeof(c->resultado.recomendacion), "%s",
             obtener_recomendacion(c->resultado.nivel_urgencia));
    return 1;
}

int cargar_todos_casos(Caso **lista_out, int *cantidad_out) {
    *lista_out = NULL;
    *cantidad_out = 0;

    FILE *f = abrir_archivo_relativo(ARCHIVO_CASOS_DEFECTO, "r", NULL, 0);
    if (!f) return 1; /* Sin archivo aun: 0 casos */

    int capacidad = 16;
    Caso *lista = malloc((size_t)capacidad * sizeof(Caso));
    if (!lista) {
        fclose(f);
        return 0;
    }

    int count = 0;
    char linea[512];
    while (fgets(linea, sizeof(linea), f)) {
        if (linea[0] == '#' || linea[0] == '\n' || linea[0] == '\r') continue;
        Caso c;
        if (!parsear_linea_caso(linea, &c)) continue;

        if (count >= capacidad) {
            capacidad *= 2;
            Caso *nuevo = realloc(lista, (size_t)capacidad * sizeof(Caso));
            if (!nuevo) {
                free(lista);
                fclose(f);
                return 0;
            }
            lista = nuevo;
        }
        lista[count++] = c;
    }
    fclose(f);

    *lista_out = lista;
    *cantidad_out = count;
    return 1;
}

int actualizar_estado_caso(int indice, int verificado, int descartado, int corregido, int tipo_correcto) {
    Caso *lista = NULL;
    int cantidad = 0;
    if (!cargar_todos_casos(&lista, &cantidad)) return 0;

    if (indice < 0 || indice >= cantidad) {
        free(lista);
        return 0;
    }

    lista[indice].verificado = verificado;
    lista[indice].descartado = descartado;
    lista[indice].diagnostico_corregido = corregido;
    if (tipo_correcto >= 0 && tipo_correcto < NUM_CLASES_ECG) {
        lista[indice].tipo_correcto = tipo_correcto;
    }

    char ruta_real[512] = ARCHIVO_CASOS_DEFECTO;
    FILE *f_check = abrir_archivo_relativo(ARCHIVO_CASOS_DEFECTO, "r", ruta_real, sizeof(ruta_real));
    if (f_check) fclose(f_check);

    FILE *f = fopen(ruta_real, "w");
    if (!f) {
        free(lista);
        return 0;
    }

    for (int i = 0; i < cantidad; i++) {
        char nombre[100], cedula[20], medico[100], fecha[20];
        sanitizar_csv(lista[i].paciente.nombre, nombre, sizeof(nombre));
        sanitizar_csv(lista[i].paciente.cedula, cedula, sizeof(cedula));
        sanitizar_csv(lista[i].paciente.medico, medico, sizeof(medico));
        sanitizar_csv(lista[i].paciente.fecha, fecha, sizeof(fecha));

        fprintf(f, "%s,%s,%s,%s,%d,%d,%d,%d,%d,%.4f,%d\n",
                nombre, cedula, medico, fecha,
                lista[i].resultado.tipo_diagnostico,
                lista[i].verificado,
                lista[i].descartado,
                lista[i].tipo_correcto,
                lista[i].diagnostico_corregido,
                lista[i].resultado.confianza,
                lista[i].usa_bmp);
    }
    fclose(f);

    /* Registrar la actualizacion medica en el reporte individual */
    char ruta_rep[160];
    snprintf(ruta_rep, sizeof(ruta_rep), "reports/%s.txt", lista[indice].paciente.cedula);
    FILE *rep = abrir_archivo_relativo(ruta_rep, "a", NULL, 0);
    if (rep) {
        fprintf(rep, "[ACTUALIZACION MEDICA]\n");
        if (descartado) {
            fprintf(rep, "  Caso marcado como DESCARTADO.\n\n");
        } else if (corregido) {
            fprintf(rep, "  Diagnostico corregido por especialista a: %s\n\n",
                    obtener_descripcion(lista[indice].tipo_correcto));
        } else if (verificado) {
            fprintf(rep, "  Diagnostico confirmado como CORRECTO.\n\n");
        }
        fclose(rep);
    }

    free(lista);
    return 1;
}

EstadisticasCasos calcular_estadisticas_casos(void) {
    EstadisticasCasos est;
    memset(&est, 0, sizeof(est));

    Caso *lista = NULL;
    int cantidad = 0;
    if (!cargar_todos_casos(&lista, &cantidad)) return est;

    est.total = cantidad;
    for (int i = 0; i < cantidad; i++) {
        int tipo_final = lista[i].diagnostico_corregido
                             ? lista[i].tipo_correcto
                             : lista[i].resultado.tipo_diagnostico;
        if (tipo_final >= 0 && tipo_final < NUM_CLASES_ECG) {
            est.por_tipo[tipo_final]++;
        }
        if (lista[i].descartado) {
            est.descartados++;
        } else if (lista[i].verificado) {
            est.verificados++;
            if (lista[i].diagnostico_corregido) est.corregidos++;
        } else {
            est.pendientes++;
        }
    }

    free(lista);
    return est;
}

char *leer_reporte_paciente(const char *cedula) {
    if (!cedula || cedula[0] == '\0') return NULL;
    char ruta[160];
    snprintf(ruta, sizeof(ruta), "reports/%s.txt", cedula);

    FILE *f = abrir_archivo_relativo(ruta, "rb", NULL, 0);
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long tam = ftell(f);
    if (tam < 0) {
        fclose(f);
        return NULL;
    }
    rewind(f);

    char *buf = malloc((size_t)tam + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }

    size_t n = fread(buf, 1, (size_t)tam, f);
    buf[n] = '\0';
    fclose(f);
    return buf;
}

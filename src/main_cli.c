#include "casos.h"
#include "classifier.h"
#include "config.h"
#include "constructor_bmp.h"
#include "dataset.h"
#include "interface.h"
#include "neural_network.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

static void registrar_verificacion_caso(Caso *caso) {
    int tipo_corr = caso->resultado.tipo_diagnostico;
    int estado = pedir_verificacion(&caso->resultado, &tipo_corr);

    caso->verificado = (estado >= 1) ? 1 : 0;
    caso->descartado = (estado == 3) ? 1 : 0;
    caso->diagnostico_corregido = (estado == 2) ? 1 : 0;
    caso->tipo_correcto = tipo_corr;
}

static void analisis_foto(redNeuronal *red_bmp) {
    Caso caso;
    memset(&caso, 0, sizeof(Caso));

    if (!registrar_paciente(&caso.paciente)) {
        printf(AMARILLO "\n  Analisis cancelado.\n" RESET);
        pausar();
        return;
    }

    caso.usa_bmp = 1;
    char ruta[256];
    printf("\n  Ruta de la imagen (.bmp): ");
    if (!LeerCadenaSegura(ruta, sizeof(ruta), 3)) {
        printf(AMARILLO "\n  Operacion cancelada.\n" RESET);
        pausar();
        return;
    }

    int ancho = 0, alto = 0;
    unsigned char *pixeles = cargar_bmp_grises(ruta, &ancho, &alto);
    if (!pixeles) {
        pausar();
        return;
    }

    int inputs = red_bmp->topologia[0];
    int lado = 32;
    unsigned char *pixeles_redi = redimensionar_grises(pixeles, ancho, alto, lado, lado);
    free(pixeles);

    if (!pixeles_redi) {
        printf(ROJO "  Error redimensionando imagen.\n" RESET);
        pausar();
        return;
    }

    float *entradas = malloc((size_t)inputs * sizeof(float));
    if (!entradas) {
        free(pixeles_redi);
        return;
    }
    preparar_entradas_bmp(pixeles_redi, entradas, inputs);
    free(pixeles_redi);

    printf(DIM "\n  Procesando red neuronal BMP...\n" RESET);
    caso.resultado = clasificar(red_bmp, entradas);
    free(entradas);

    mostrar_resultado(caso.paciente, caso.resultado);
    registrar_verificacion_caso(&caso);

    if (guardar_caso_clinico(&caso)) {
        printf(VERDE "\n  Caso guardado y reporte generado.\n" RESET);
    } else {
        printf(ROJO "\n  Advertencia: no se pudo guardar el caso.\n" RESET);
    }
    pausar();
}

static void analisis_manual(redNeuronal *red_manual) {
    Caso caso;
    memset(&caso, 0, sizeof(Caso));

    if (!registrar_paciente(&caso.paciente)) {
        printf(AMARILLO "\n  Analisis cancelado.\n" RESET);
        pausar();
        return;
    }

    caso.usa_bmp = 0;
    float *entradas = ingresar_intervalos_manual();
    if (!entradas) {
        printf(AMARILLO "\n  Analisis cancelado.\n" RESET);
        pausar();
        return;
    }

    for (int i = 0; i < 5; i++) {
        caso.intervalos_manuales[i] = entradas[i];
    }

    float entradas_norm[5];
    normalizar_variables_clinicas(entradas, entradas_norm);
    free(entradas);

    printf(DIM "\n  Procesando red neuronal Clinica...\n" RESET);
    caso.resultado = clasificar(red_manual, entradas_norm);

    mostrar_resultado(caso.paciente, caso.resultado);
    registrar_verificacion_caso(&caso);

    if (guardar_caso_clinico(&caso)) {
        printf(VERDE "\n  Caso guardado y reporte generado.\n" RESET);
    } else {
        printf(ROJO "\n  Advertencia: no se pudo guardar el caso.\n" RESET);
    }
    pausar();
}

static void reentrenar_redes(redNeuronal *red_bmp, redNeuronal *red_manual, const Configuracion *cfg) {
    mostrar_encabezado();
    printf(AMARILLO "\n  === RE-ENTRENAMIENTO DE REDES ===\n\n" RESET);

    printf(DIM "  [1/2] Entrenando Red BMP...\n" RESET);
    datos *ds_bmp = cargar_dataset(cfg->dataset_bmp, cfg->topologia_bmp[0],
                                   cfg->topologia_bmp[cfg->num_capas_bmp - 1]);
    if (ds_bmp) {
        EntrenamientoResumen res;
        reinicializar_pesos(red_bmp);
        entrenar_red_con_log(red_bmp, ds_bmp->entradas, ds_bmp->salidas,
                             ds_bmp->ejemplos, ds_bmp->num_salida,
                             "reports/training_log_bmp.csv", &res);
        guardar_pesos(red_bmp, cfg->pesos_bmp);
        liberarDatos(ds_bmp);
        printf(VERDE "  Red BMP lista (Precision: %.1f%%, Pesos guardados).\n" RESET, res.precision_final);
    }

    printf(DIM "\n  [2/2] Entrenando Red Manual...\n" RESET);
    datos *ds_manual = cargar_dataset(cfg->dataset_manual, cfg->topologia_manual[0],
                                      cfg->topologia_manual[cfg->num_capas_manual - 1]);
    if (ds_manual) {
        EntrenamientoResumen res;
        reinicializar_pesos(red_manual);
        entrenar_red_con_log(red_manual, ds_manual->entradas, ds_manual->salidas,
                             ds_manual->ejemplos, ds_manual->num_salida,
                             "reports/training_log_manual.csv", &res);
        guardar_pesos(red_manual, cfg->pesos_manual);
        liberarDatos(ds_manual);
        printf(VERDE "  Red Manual lista (Precision: %.1f%%, Pesos guardados).\n" RESET, res.precision_final);
    }
    pausar();
}

static void menu_administrador(redNeuronal *red_bmp, redNeuronal *red_manual, const Configuracion *cfg) {
    char contrasena[64];
    mostrar_encabezado();
    printf(AMARILLO "\n  === ACCESO ADMINISTRADOR ===\n\n" RESET);
    printf("  Contrasena (PIN): ");
    if (!LeerCadenaSegura(contrasena, sizeof(contrasena), 3)) {
        return;
    }

    if (strcmp(contrasena, cfg->admin_pin) != 0) {
        printf(ROJO "\n  Contrasena incorrecta.\n" RESET);
        pausar();
        return;
    }

    int op = 0;
    while (op != 6) {
        mostrar_menu_admin();
        op = pedir_opcion(1, 6, 1);

        switch (op) {
            case 1:
                gestionar_casos();
                break;
            case 2:
                mostrar_estadisticas();
                break;
            case 3: {
                mostrar_encabezado();
                printf(AMARILLO "\n  === GENERAR REPORTE ===\n\n" RESET);
                printf("  Ingrese cedula del paciente: ");
                long ced = pedir_cedula();
                if (ced != -1) {
                    char cedula[20];
                    snprintf(cedula, sizeof(cedula), "%ld", ced);
                    mostrar_reporte(cedula);
                }
                break;
            }
            case 4:
                reentrenar_redes(red_bmp, red_manual, cfg);
                break;
            case 5:
                graficar_error_ascii("reports/training_log_bmp.csv", "RED BMP");
                graficar_error_ascii("reports/training_log_manual.csv", "RED MANUAL");
                break;
            case 6:
                break;
        }
    }
}

int main(void) {
    /* Habilitar soporte UTF-8 y colores ANSI en consola Windows */
    SetConsoleOutputCP(CP_UTF8);
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (GetConsoleMode(hOut, &mode)) {
        SetConsoleMode(hOut, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    }

    mostrar_encabezado();
    if (!pedir_confirmacion("\n  Iniciar sistema clinico SINA-VISUAL CLI? (S/N): ")) {
        printf(DIM "  Saliendo...\n" RESET);
        return 0;
    }

    printf(DIM "\n  Cargando configuracion... " RESET);
    Configuracion cfg = cargar_config("data/config.txt");
    printf(VERDE "OK\n" RESET);

    redNeuronal *red_bmp = inicializar_red(cfg.num_capas_bmp, cfg.topologia_bmp,
                                           cfg.tasa_bmp, cfg.epocas_bmp);
    redNeuronal *red_manual = inicializar_red(cfg.num_capas_manual, cfg.topologia_manual,
                                              cfg.tasa_manual, cfg.epocas_manual);

    printf(DIM "  Cargando pesos guardados... " RESET);
    int bmp_ok = cargar_pesos(red_bmp, cfg.pesos_bmp);
    int man_ok = cargar_pesos(red_manual, cfg.pesos_manual);
    printf("%s\n", (bmp_ok && man_ok) ? VERDE "Cargados" RESET : AMARILLO "Pendientes (use Admin -> Re-entrenar)" RESET);

    pausar();

    int opcion = 0;
    while (opcion != 5) {
        mostrar_menu_principal();
        opcion = pedir_opcion(1, 5, 0);

        switch (opcion) {
            case 1:
                if (!red_bmp->entrenada) {
                    printf(ROJO "\n  ERROR: La Red BMP no tiene pesos entrenados.\n" RESET);
                    printf(AMARILLO "  Ingrese al menu Administrador (Opcion 4) para entrenarla.\n" RESET);
                    pausar();
                } else {
                    analisis_foto(red_bmp);
                }
                break;
            case 2:
                if (!red_manual->entrenada) {
                    printf(ROJO "\n  ERROR: La Red Manual no tiene pesos entrenados.\n" RESET);
                    printf(AMARILLO "  Ingrese al menu Administrador (Opcion 4) para entrenarla.\n" RESET);
                    pausar();
                } else {
                    analisis_manual(red_manual);
                }
                break;
            case 3: {
                mostrar_encabezado();
                printf("\n  Ingrese la cedula del paciente a buscar (ESC para cancelar): ");
                long ced = pedir_cedula();
                if (ced != -1) {
                    char cedula[20];
                    snprintf(cedula, sizeof(cedula), "%ld", ced);
                    mostrar_reporte(cedula);
                }
                break;
            }
            case 4:
                menu_administrador(red_bmp, red_manual, &cfg);
                break;
            case 5:
                break;
        }
    }

    liberar_red(red_bmp);
    liberar_red(red_manual);
    printf(VERDE "\n  Hasta luego.\n" RESET);
    return 0;
}

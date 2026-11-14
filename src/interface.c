#include "interface.h"
#include "casos.h"
#include "config.h"
#include <conio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

void limpiar_pantalla(void) {
    system("cls");
}

void pausar(void) {
    printf("\n  Presione cualquier tecla para continuar...");
    /* Limpiar buffer pendiente de teclado antes de esperar */
    while (_kbhit()) {
        _getch();
    }
    int c = _getch();
    if (c == 0 || c == 224) {
        _getch();
    }
    printf("\n");
}

void mostrar_encabezado(void) {
    limpiar_pantalla();
    printf(CYAN "\n=======================================================\n");
    printf("         ****** SINA-VISUAL (CLI) *******\n");
    printf("=======================================================\n" RESET);
}

void mostrar_menu_principal(void) {
    mostrar_encabezado();
    printf("\n");
    printf(BLANCO "  1." RESET " Nuevo analisis (Imagen BMP)\n");
    printf(BLANCO "  2." RESET " Nuevo analisis (Variables Manuales)\n");
    printf(BLANCO "  3." RESET " Buscar paciente\n");
    printf(BLANCO "  4." RESET " Administrador\n");
    printf(BLANCO "  5." RESET " Salir\n");
    printf("\n");
    printf(DIM "  ----------------------------------------\n" RESET);
    printf("  Seleccione una opcion: ");
}

int LeerCadenaSegura(char cadena[], int limite, int modo) {
    int i = 0;
    int tecla;

    while (1) {
        tecla = _getch();
        if (tecla == 0 || tecla == 224) {
            _getch();
            continue;
        }
        if (tecla == 27) {
            return 0;
        }
        if (tecla == 13) {
            if (i > 0) {
                cadena[i] = '\0';
                printf("\n");
                return 1;
            }
            continue;
        }
        if (tecla == 8) {
            if (i > 0) {
                i--;
                printf("\b \b");
            }
            continue;
        }
        if (tecla >= 1 && tecla <= 31) {
            continue;
        }

        if (i < (limite - 1)) {
            int valido = 0;
            if (modo == 0) {
                if ((tecla >= 'A' && tecla <= 'Z') || (tecla >= 'a' && tecla <= 'z') || tecla == 32)
                    valido = 1;
            } else if (modo == 1) {
                if (tecla >= '0' && tecla <= '9')
                    valido = 1;
            } else if (modo == 2) {
                if ((tecla >= '0' && tecla <= '9') || tecla == '/')
                    valido = 1;
            } else if (modo == 3) {
                if (tecla >= 32 && tecla <= 126)
                    valido = 1;
            } else if (modo == 4) {
                if ((tecla >= '0' && tecla <= '9') || tecla == '.')
                    valido = 1;
            }

            if (valido) {
                cadena[i++] = (char)tecla;
                printf("%c", tecla);
            }
        }
    }
}

static int pedir_fecha(char *cadena) {
    int i = 0;
    int tecla;

    do {
        tecla = _getch();
        if (tecla == 0 || tecla == 224) {
            _getch();
            continue;
        }
        if (tecla == 27) {
            return 0;
        }
        if (tecla == 8) {
            if (i > 0) {
                if (i == 3 || i == 6) {
                    i -= 2;
                    printf("\b\b  \b\b");
                } else {
                    i--;
                    printf("\b \b");
                }
            }
            continue;
        }

        if (tecla >= '0' && tecla <= '9' && i < 10) {
            if (i == 2 || i == 5) {
                cadena[i++] = '/';
                printf("/");
            }
            if (i < 10) {
                cadena[i++] = (char)tecla;
                printf("%c", tecla);
            }
        }

        cadena[i] = '\0';

        if (tecla == 13 && i == 10) {
            int dia, mes, years;
            int valida = 1;
            if (sscanf(cadena, "%d/%d/%d", &dia, &mes, &years) == 3) {
                if (years < 1900 || years > 2100 || mes < 1 || mes > 12) {
                    valida = 0;
                } else {
                    int dias_mes[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
                    if ((years % 4 == 0 && years % 100 != 0) || (years % 400 == 0))
                        dias_mes[2] = 29;
                    if (dia < 1 || dia > dias_mes[mes])
                        valida = 0;
                }
            } else {
                valida = 0;
            }

            if (!valida) {
                printf("\n" ROJO "  [ERROR] Fecha ilogica. Intente de nuevo: " RESET);
                i = 0;
                tecla = 0;
            }
        }
    } while (tecla != 13 || i < 10);

    printf("\n");
    return 1;
}

int registrar_paciente(Paciente *p) {
    mostrar_encabezado();
    printf(AMARILLO "\n  === REGISTRO DE PACIENTE (ESC para cancelar) ===\n\n" RESET);

    printf("  Nombre completo : ");
    if (!LeerCadenaSegura(p->nombre, 60, 0)) return 0;

    printf("  Cedula          : ");
    long ced = pedir_cedula();
    if (ced == -1) return 0;
    snprintf(p->cedula, sizeof(p->cedula), "%ld", ced);

    printf("  Medico tratante : ");
    if (!LeerCadenaSegura(p->medico, 60, 0)) return 0;

    printf("  Fecha (DD/MM/AAAA): ");
    if (!pedir_fecha(p->fecha)) return 0;

    return 1;
}

float *ingresar_intervalos_manual(void) {
    float *entradas = malloc(5 * sizeof(float));
    if (!entradas) return NULL;

    char buffer[20];
    mostrar_encabezado();
    printf(AMARILLO "\n  === INGRESO MANUAL DE INTERVALOS ===\n\n" RESET);
    printf("  Ingresar valores brutos:\n\n");

    const char *etiquetas[5] = {
        "  RR  (ms) : ",
        "  PR  (ms) : ",
        "  QRS (ms) : ",
        "  QT  (ms) : ",
        "  BPM (bpm): "
    };

    for (int i = 0; i < 5; i++) {
        printf("%s", etiquetas[i]);
        if (!LeerCadenaSegura(buffer, 15, 4)) {
            free(entradas);
            return NULL;
        }
        entradas[i] = (float)atof(buffer);
    }

    return entradas;
}

void mostrar_resultado(Paciente paciente, ResultadoECG resultado) {
    mostrar_encabezado();
    printf(AMARILLO "\n  === RESULTADO DEL ANALISIS ===\n\n" RESET);

    printf("  Paciente : %s\n", paciente.nombre);
    printf("  Cedula   : %s\n", paciente.cedula);
    printf("  Medico   : %s\n", paciente.medico);

    printf("\n  Diagnostico : ");
    if (resultado.hay_arritmia) {
        printf(ROJO "%s\n" RESET, resultado.descripcion);
    } else {
        printf(VERDE "%s\n" RESET, resultado.descripcion);
    }

    printf("  Confianza   : " CYAN "%.1f%%\n" RESET, resultado.confianza * 100.0f);
    printf("\n  Distribucion de salidas:\n");
    for (int i = 0; i < NUM_CLASES_ECG; i++) {
        printf("    - %-26s : %5.1f%%\n",
               obtener_descripcion(i), resultado.probabilidades[i] * 100.0f);
    }

    printf("\n  Urgencia    : ");
    if (resultado.nivel_urgencia == URGENCIA_ALTA)
        printf(ROJO "ALTA - Atencion Inmediata\n" RESET);
    else if (resultado.nivel_urgencia == URGENCIA_MEDIA)
        printf(AMARILLO "MEDIA - Consulta Cardiologia\n" RESET);
    else
        printf(VERDE "NINGUNA - Control Regular\n" RESET);

    printf("\n  Recomendacion: %s\n", resultado.recomendacion);
}

int pedir_verificacion(ResultadoECG *resultado, int *tipo_corregido_out) {
    printf("\n  ----------------------------------------\n");
    printf("  Verificacion medica:\n");
    printf(BLANCO "  1." RESET " Diagnostico correcto\n");
    printf(BLANCO "  2." RESET " Corregir diagnostico\n");
    printf(BLANCO "  3." RESET " Descartar caso (Mala lectura/Ruido)\n");
    printf("\n  Opcion: ");

    int op = pedir_opcion(1, 3, 1);
    *tipo_corregido_out = resultado->tipo_diagnostico;

    if (op == 2) {
        printf("\n  Seleccione el diagnostico clinico correcto:\n");
        printf("    1. %s\n", obtener_descripcion(TIPO_NORMAL));
        printf("    2. %s\n", obtener_descripcion(TIPO_ARRITMIA));
        printf("    3. %s\n", obtener_descripcion(TIPO_INFARTO));
        printf("    4. %s\n", obtener_descripcion(TIPO_HISTORIAL));
        printf("  Diagnostico correcto (1-4): ");
        int corr = pedir_opcion(1, 4, 0);
        *tipo_corregido_out = corr - 1;
    }
    return op;
}

void mostrar_reporte(const char *cedula) {
    mostrar_encabezado();
    printf(AMARILLO "\n  === REPORTE CLINICO ===\n\n" RESET);

    char *contenido = leer_reporte_paciente(cedula);
    if (!contenido) {
        printf(ROJO "  Paciente con cedula %s no encontrado.\n" RESET, cedula);
        pausar();
        return;
    }

    printf("%s", contenido);
    free(contenido);
    pausar();
}

void mostrar_menu_admin(void) {
    mostrar_encabezado();
    printf(AMARILLO "\n  === ADMINISTRADOR ===\n\n" RESET);
    printf(BLANCO "  1." RESET " Gestionar casos\n");
    printf(BLANCO "  2." RESET " Estadisticas del Consultorio\n");
    printf(BLANCO "  3." RESET " Generar Reporte de Paciente\n");
    printf(BLANCO "  4." RESET " Re-entrenar Redes Neuronales\n");
    printf(BLANCO "  5." RESET " Ver grafico de evolucion del error\n");
    printf(BLANCO "  6." RESET " Volver al menu principal\n");
    printf("\n");
    printf(DIM "  ----------------------------------------\n" RESET);
    printf("  Seleccione una opcion: ");
}

void gestionar_casos(void) {
    int opcion = 0;
    while (opcion != 4) {
        Caso *lista = NULL;
        int cantidad = 0;
        if (!cargar_todos_casos(&lista, &cantidad)) {
            printf(ROJO "  Error leyendo casos.\n" RESET);
            pausar();
            return;
        }

        if (cantidad == 0) {
            free(lista);
            mostrar_encabezado();
            printf(AMARILLO "\n  === GESTIONAR CASOS ===\n\n" RESET);
            printf(DIM "  No hay casos registrados aun.\n" RESET);
            pausar();
            return;
        }

        int pendientes = 0, verificados = 0, descartados = 0;
        for (int i = 0; i < cantidad; i++) {
            if (lista[i].descartado) descartados++;
            else if (lista[i].verificado) verificados++;
            else pendientes++;
        }

        mostrar_encabezado();
        printf(AMARILLO "\n  === GESTIONAR CASOS ===\n\n" RESET);
        printf("  Pendientes  : " AMARILLO "%d\n" RESET, pendientes);
        printf("  Verificados : " VERDE "%d\n" RESET, verificados);
        printf("  Descartados : " ROJO "%d\n" RESET, descartados);
        printf(DIM "\n  ----------------------------------------\n" RESET);
        printf(BLANCO "  1." RESET " Revisar casos pendientes\n");
        printf(BLANCO "  2." RESET " Ver casos verificados\n");
        printf(BLANCO "  3." RESET " Ver casos descartados\n");
        printf(BLANCO "  4." RESET " Volver\n");
        printf("\n  Opcion: ");
        opcion = pedir_opcion(1, 4, 1);

        if (opcion == 1 && pendientes > 0) {
            for (int i = 0; i < cantidad; i++) {
                if (lista[i].verificado == 0 && lista[i].descartado == 0) {
                    mostrar_encabezado();
                    printf(AMARILLO "\n  === REVISANDO CASO #%d ===\n\n" RESET, i + 1);
                    printf("  Paciente          : %s\n", lista[i].paciente.nombre);
                    printf("  Cedula            : %s\n", lista[i].paciente.cedula);
                    printf("  Diagnostico IA    : %s (%.1f%%)\n",
                           obtener_descripcion(lista[i].resultado.tipo_diagnostico),
                           lista[i].resultado.confianza * 100.0f);
                    printf("\n  Que desea hacer?\n");
                    printf("  1. Verificar (Correcto)\n");
                    printf("  2. Corregir diagnostico\n");
                    printf("  3. Descartar (Mala lectura)\n");
                    printf("  4. Saltar por ahora\n");
                    printf("\n  Opcion: ");
                    int accion = pedir_opcion(1, 4, 1);

                    if (accion == 1) {
                        actualizar_estado_caso(i, 1, 0, 0, lista[i].resultado.tipo_diagnostico);
                    } else if (accion == 2) {
                        printf("\n  Seleccione diagnostico correcto (1=Normal, 2=Arritmia, 3=Infarto, 4=Post-MI): ");
                        int corr = pedir_opcion(1, 4, 0);
                        actualizar_estado_caso(i, 1, 0, 1, corr - 1);
                    } else if (accion == 3) {
                        actualizar_estado_caso(i, 1, 1, 0, lista[i].resultado.tipo_diagnostico);
                    }
                }
            }
        } else if (opcion == 2 || opcion == 3) {
            mostrar_encabezado();
            printf(AMARILLO "\n  === LISTA DE CASOS ===\n\n" RESET);
            for (int i = 0; i < cantidad; i++) {
                if ((opcion == 2 && lista[i].verificado && !lista[i].descartado) ||
                    (opcion == 3 && lista[i].descartado)) {
                    int tipo_show = lista[i].diagnostico_corregido
                                        ? lista[i].tipo_correcto
                                        : lista[i].resultado.tipo_diagnostico;
                    printf("  - %s (C.I: %s) -> %s%s\n",
                           lista[i].paciente.nombre,
                           lista[i].paciente.cedula,
                           obtener_descripcion(tipo_show),
                           lista[i].diagnostico_corregido ? " [Corregido]" : "");
                }
            }
            pausar();
        }
        free(lista);
    }
}

void mostrar_estadisticas(void) {
    mostrar_encabezado();
    printf(AMARILLO "\n  === ESTADISTICAS DEL CONSULTORIO ===\n\n" RESET);

    EstadisticasCasos est = calcular_estadisticas_casos();
    if (est.total == 0) {
        printf(DIM "  No hay casos registrados aun.\n" RESET);
        pausar();
        return;
    }

    printf("  Total pacientes analizados : " BLANCO "%d\n\n" RESET, est.total);
    const char *nombres[] = {"Normal      ", "Arritmia    ", "Infarto (MI)", "Historial MI"};

    printf("  Resultados:\n");
    for (int i = 0; i < 4; i++) {
        int barras = (est.por_tipo[i] * 20) / est.total;
        int pct = (est.por_tipo[i] * 100) / est.total;
        printf("  %s [", nombres[i]);
        for (int j = 0; j < 20; j++)
            printf(j < barras ? VERDE "#" RESET : DIM " " RESET);
        printf("] %3d%% (%d)\n", pct, est.por_tipo[i]);
    }

    printf(DIM "\n  ----------------------------------------\n" RESET);
    printf("  Casos verificados : " VERDE "%d\n" RESET, est.verificados);
    printf("  Casos corregidos  : " CYAN "%d\n" RESET, est.corregidos);
    printf("  Casos pendientes  : " AMARILLO "%d\n" RESET, est.pendientes);
    printf("  Casos descartados : " ROJO "%d\n" RESET, est.descartados);
    pausar();
}

void graficar_error_ascii(const char *archivo_log, const char *titulo) {
    mostrar_encabezado();
    printf(AMARILLO "\n  === EVOLUCION DEL ERROR (%s) ===\n\n" RESET, titulo ? titulo : "RED");

    FILE *f = abrir_archivo_relativo(archivo_log, "r", NULL, 0);
    if (!f) {
        printf(DIM "  No hay datos de entrenamiento en %s aun.\n" RESET, archivo_log);
        pausar();
        return;
    }

    double errores[1000];
    int epocas[1000];
    int count = 0;
    double max_error = 0.0;

    char linea[128];
    while (fgets(linea, sizeof(linea), f) && count < 1000) {
        int ep = 0;
        double err = 0.0;
        if (sscanf(linea, "%d,%lf", &ep, &err) == 2) {
            epocas[count] = ep;
            errores[count] = err;
            if (err > max_error) max_error = err;
            count++;
        }
    }
    fclose(f);

    if (count == 0) {
        printf(DIM "  Log vacio.\n" RESET);
        pausar();
        return;
    }
    if (max_error <= 0.0) max_error = 1.0;

    int paso = count > 20 ? count / 20 : 1;
    for (int i = 0; i < count; i += paso) {
        int barras = (int)((errores[i] / max_error) * 30.0);
        printf("  %4d | ", epocas[i] + 1);
        for (int j = 0; j < barras; j++) {
            if (errores[i] > 0.3) printf(ROJO "*" RESET);
            else if (errores[i] > 0.1) printf(AMARILLO "*" RESET);
            else printf(VERDE "*" RESET);
        }
        printf(" %.4f\n", errores[i]);
    }

    printf(DIM "\n  ----------------------------------------\n" RESET);
    printf("  Error inicial : " ROJO "%.4f\n" RESET, errores[0]);
    printf("  Error final   : " VERDE "%.4f\n" RESET, errores[count - 1]);
    if (errores[0] > 0.0) {
        printf("  Mejora        : " VERDE "%.1f%%\n" RESET,
               ((errores[0] - errores[count - 1]) / errores[0]) * 100.0);
    }
    pausar();
}

int pedir_opcion(int min, int max, int allow_esc) {
    int num;
    long pEntera = 0;
    int cEnteros = 0;

    do {
        num = _getch();
        if (num == 0 || num == 224) {
            _getch();
            continue;
        }
        if (num == 27) {
            if (allow_esc) {
                printf("\n");
                return max;
            }
            continue;
        }
        if (num == 8) {
            if (cEnteros > 0) {
                pEntera /= 10;
                cEnteros--;
                printf("\b \b");
            }
            continue;
        }
        if (num >= '0' && num <= '9') {
            if (cEnteros == 0 && num == '0') continue;
            long temp = (pEntera * 10) + (num - '0');
            if (temp <= max) {
                pEntera = temp;
                cEnteros++;
                printf("%c", num);
            }
        }
    } while (num != 13 || pEntera < min || pEntera > max);

    printf("\n");
    return (int)pEntera;
}

long pedir_cedula(void) {
    int num;
    long pEntera = 0;
    int cEnteros = 0;

    do {
        num = _getch();
        if (num == 0 || num == 224) {
            _getch();
            continue;
        }
        if (num == 27) {
            printf("\n");
            return -1;
        }
        if (num == 8) {
            if (cEnteros > 0) {
                pEntera /= 10;
                cEnteros--;
                printf("\b \b");
            }
            continue;
        }
        if (num >= '0' && num <= '9') {
            if (cEnteros == 0 && num == '0') continue;
            long temp = (pEntera * 10) + (num - '0');
            if (temp <= 50000000) {
                pEntera = temp;
                cEnteros++;
                printf("%c", num);
            }
        }
    } while (num != 13 || pEntera < 4000000);

    printf("\n");
    return pEntera;
}

int pedir_confirmacion(const char *mensaje) {
    printf("%s", mensaje);
    while (1) {
        int tecla = _getch();
        if (tecla == 0 || tecla == 224) {
            _getch();
            continue;
        }
        if (tecla == 'S' || tecla == 's') {
            printf("S\n");
            return 1;
        } else if (tecla == 'N' || tecla == 'n') {
            printf("N\n");
            return 0;
        }
    }
}

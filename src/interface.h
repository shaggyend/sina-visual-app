#ifndef INTERFACE_H
#define INTERFACE_H

#include "classifier.h"

#define RESET    "\033[0m"
#define ROJO     "\033[1;31m"
#define VERDE    "\033[1;32m"
#define AMARILLO "\033[1;33m"
#define CYAN     "\033[1;36m"
#define BLANCO   "\033[1;37m"
#define DIM      "\033[2m"

void limpiar_pantalla(void);
void mostrar_encabezado(void);
void mostrar_menu_principal(void);
void mostrar_resultado(Paciente paciente, ResultadoECG resultado);
void mostrar_estadisticas(void);
void mostrar_reporte(const char *cedula);
int pedir_verificacion(ResultadoECG *resultado, int *tipo_corregido_out);
int registrar_paciente(Paciente *p);
int LeerCadenaSegura(char cadena[], int limite, int modo);
float *ingresar_intervalos_manual(void);
void pausar(void);
void mostrar_menu_admin(void);
void gestionar_casos(void);
void graficar_error_ascii(const char *archivo_log, const char *titulo);
int pedir_opcion(int min, int max, int allow_esc);
long pedir_cedula(void);
int pedir_confirmacion(const char *mensaje);

#endif

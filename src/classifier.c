#include "classifier.h"
#include <stdio.h>
#include <string.h>

ResultadoECG clasificar(redNeuronal *red, float *entradas) {
    ResultadoECG res;
    memset(&res, 0, sizeof(ResultadoECG));
    if (!red || !entradas) return res;

    forwardPropagation(red, entradas);
    capaRedNeurona *salida = &red->capa[red->nume_capas - 1];

    int n = salida->cantidad_neuronas;
    if (n > NUM_CLASES_ECG) n = NUM_CLASES_ECG;

    int indice_max = 0;
    float prob_max = salida->activaciones[0];
    for (int i = 0; i < n; i++) {
        res.probabilidades[i] = salida->activaciones[i];
        if (salida->activaciones[i] > prob_max) {
            prob_max = salida->activaciones[i];
            indice_max = i;
        }
    }

    res.tipo_diagnostico = indice_max;
    res.confianza = prob_max;
    res.hay_arritmia = (indice_max != TIPO_NORMAL) ? 1 : 0;
    res.nivel_urgencia = calcular_urgencia(res.tipo_diagnostico, res.confianza);

    snprintf(res.descripcion, sizeof(res.descripcion), "%s", obtener_descripcion(res.tipo_diagnostico));
    snprintf(res.recomendacion, sizeof(res.recomendacion), "%s", obtener_recomendacion(res.nivel_urgencia));

    return res;
}

int calcular_urgencia(int tipo_diagnostico, float confianza) {
    if (tipo_diagnostico == TIPO_NORMAL) return URGENCIA_NINGUNA;
    if (tipo_diagnostico == TIPO_HISTORIAL) return URGENCIA_MEDIA;
    if (tipo_diagnostico == TIPO_INFARTO) return URGENCIA_ALTA;
    if (confianza > 0.85f) return URGENCIA_ALTA;
    return URGENCIA_MEDIA;
}

const char *obtener_descripcion(int tipo) {
    switch (tipo) {
        case TIPO_NORMAL:    return "Ritmo Sinusal Normal";
        case TIPO_ARRITMIA:  return "Arritmia Detectada";
        case TIPO_INFARTO:   return "Infarto de Miocardio (MI)";
        case TIPO_HISTORIAL: return "Historial Post-MI";
        default:             return "Desconocido";
    }
}

const char *obtener_recomendacion(int urgencia) {
    switch (urgencia) {
        case URGENCIA_NINGUNA: return "Mantener control regular. No requiere accion inmediata.";
        case URGENCIA_MEDIA:   return "Programar consulta con cardiologo para evaluacion detallada.";
        case URGENCIA_ALTA:    return "ATENCION MEDICA INMEDIATA REQUERIDA. Dirigirse a urgencias.";
        default:               return "Sin recomendacion.";
    }
}

const char *obtener_texto_urgencia(int urgencia) {
    switch (urgencia) {
        case URGENCIA_ALTA:    return "ALTA";
        case URGENCIA_MEDIA:   return "MEDIA";
        default:               return "NINGUNA";
    }
}

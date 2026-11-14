#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <shellapi.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "casos.h"
#include "classifier.h"
#include "config.h"
#include "constructor_bmp.h"
#include "dataset.h"
#include "neural_network.h"

#define MAX_BODY_BYTES (32 * 1024 * 1024)
#define RECV_CHUNK 65536

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} StrBuf;

static int sb_init(StrBuf *sb, size_t initial_cap) {
    sb->cap = initial_cap > 256 ? initial_cap : 256;
    sb->len = 0;
    sb->data = malloc(sb->cap);
    if (!sb->data) return 0;
    sb->data[0] = '\0';
    return 1;
}

static void sb_free(StrBuf *sb) {
    free(sb->data);
    sb->data = NULL;
    sb->len = 0;
    sb->cap = 0;
}

static int sb_append_n(StrBuf *sb, const char *s, size_t n) {
    if (sb->len + n + 1 > sb->cap) {
        size_t nueva_cap = sb->cap * 2;
        while (nueva_cap < sb->len + n + 1) nueva_cap *= 2;
        char *nuevo = realloc(sb->data, nueva_cap);
        if (!nuevo) return 0;
        sb->data = nuevo;
        sb->cap = nueva_cap;
    }
    memcpy(sb->data + sb->len, s, n);
    sb->len += n;
    sb->data[sb->len] = '\0';
    return 1;
}

static int sb_append(StrBuf *sb, const char *s) {
    return s ? sb_append_n(sb, s, strlen(s)) : 1;
}

static int sb_printf(StrBuf *sb, const char *fmt, ...) {
    char tmp[1024];
    va_list args;
    va_start(args, fmt);
    int needed = vsnprintf(tmp, sizeof(tmp), fmt, args);
    va_end(args);
    if (needed < 0) return 0;
    if ((size_t)needed < sizeof(tmp)) {
        return sb_append_n(sb, tmp, (size_t)needed);
    }
    char *dyn = malloc((size_t)needed + 1);
    if (!dyn) return 0;
    va_start(args, fmt);
    vsnprintf(dyn, (size_t)needed + 1, fmt, args);
    va_end(args);
    int ok = sb_append_n(sb, dyn, (size_t)needed);
    free(dyn);
    return ok;
}

static void sb_append_json_str(StrBuf *sb, const char *s) {
    sb_append(sb, "\"");
    if (s) {
        for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
            unsigned char c = *p;
            if (c == '\"') sb_append(sb, "\\\"");
            else if (c == '\\') sb_append(sb, "\\\\");
            else if (c == '\b') sb_append(sb, "\\b");
            else if (c == '\f') sb_append(sb, "\\f");
            else if (c == '\n') sb_append(sb, "\\n");
            else if (c == '\r') sb_append(sb, "\\r");
            else if (c == '\t') sb_append(sb, "\\t");
            else if (c < 0x20) sb_printf(sb, "\\u%04x", c);
            else sb_append_n(sb, (const char *)p, 1);
        }
    }
    sb_append(sb, "\"");
}

/* Estado global de la aplicacion */
typedef struct {
    Configuracion cfg;
    redNeuronal *red_bmp;
    redNeuronal *red_manual;
    EntrenamientoResumen resumen_bmp;
    EntrenamientoResumen resumen_manual;
    int tiene_resumen_bmp;
    int tiene_resumen_manual;
    volatile int ejecutando;
} AppState;

static int hex_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static void url_decode(const char *src, char *dst, size_t max_len) {
    size_t i = 0;
    while (*src && i + 1 < max_len) {
        if (*src == '%' && hex_val(src[1]) >= 0 && hex_val(src[2]) >= 0) {
            dst[i++] = (char)((hex_val(src[1]) << 4) | hex_val(src[2]));
            src += 3;
        } else if (*src == '+') {
            dst[i++] = ' ';
            src++;
        } else {
            dst[i++] = *src++;
        }
    }
    dst[i] = '\0';
}

static void obtener_query_param(const char *query, const char *clave, char *out, size_t max_len) {
    out[0] = '\0';
    if (!query || !clave) return;

    size_t klen = strlen(clave);
    const char *p = query;
    while (*p) {
        const char *amp = strchr(p, '&');
        size_t seg_len = amp ? (size_t)(amp - p) : strlen(p);

        const char *eq = memchr(p, '=', seg_len);
        if (eq && (size_t)(eq - p) == klen && strncmp(p, clave, klen) == 0) {
            size_t vlen = seg_len - klen - 1;
            char encoded[512];
            if (vlen >= sizeof(encoded)) vlen = sizeof(encoded) - 1;
            memcpy(encoded, eq + 1, vlen);
            encoded[vlen] = '\0';
            url_decode(encoded, out, max_len);
            return;
        }
        if (!amp) break;
        p = amp + 1;
    }
}

/* Lectura simple y segura de campos JSON planos */
static int json_get_string(const char *json, const char *key, char *out, size_t out_sz) {
    out[0] = '\0';
    if (!json || !key) return 0;
    char patron[128];
    snprintf(patron, sizeof(patron), "\"%s\"", key);
    const char *p = strstr(json, patron);
    if (!p) return 0;
    p += strlen(patron);
    while (*p && (*p == ' ' || *p == '\t' || *p == ':')) p++;
    if (*p != '\"') return 0;
    p++;

    size_t i = 0;
    while (*p && *p != '\"' && i + 1 < out_sz) {
        if (*p == '\\' && p[1]) {
            p++;
            if (*p == 'n') out[i++] = '\n';
            else if (*p == 'r') out[i++] = '\r';
            else if (*p == 't') out[i++] = '\t';
            else out[i++] = *p;
        } else {
            out[i++] = *p;
        }
        p++;
    }
    out[i] = '\0';
    return 1;
}

static int json_get_double(const char *json, const char *key, double *out) {
    if (!json || !key) return 0;
    char patron[128];
    snprintf(patron, sizeof(patron), "\"%s\"", key);
    const char *p = strstr(json, patron);
    if (!p) return 0;
    p += strlen(patron);
    while (*p && (*p == ' ' || *p == '\t' || *p == ':')) p++;
    if (!*p || (*p != '-' && !isdigit((unsigned char)*p))) return 0;
    *out = strtod(p, NULL);
    return 1;
}

static void enviar_respuesta(SOCKET sock, int status_code, const char *status_text,
                             const char *content_type, const char *body, size_t body_len) {
    char headers[512];
    int hlen = snprintf(headers, sizeof(headers),
                        "HTTP/1.1 %d %s\r\n"
                        "Content-Type: %s\r\n"
                        "Content-Length: %zu\r\n"
                        "Cache-Control: no-store\r\n"
                        "Connection: close\r\n"
                        "\r\n",
                        status_code, status_text, content_type, body_len);

    send(sock, headers, hlen, 0);
    size_t sent = 0;
    while (sent < body_len) {
        int chunk = (int)((body_len - sent) > 65536 ? 65536 : (body_len - sent));
        int n = send(sock, body + sent, chunk, 0);
        if (n <= 0) break;
        sent += (size_t)n;
    }
}

static void enviar_json(SOCKET sock, int code, const char *status_text, const StrBuf *sb) {
    enviar_respuesta(sock, code, status_text, "application/json; charset=utf-8", sb->data, sb->len);
}

static void enviar_json_error(SOCKET sock, int code, const char *msg) {
    StrBuf sb;
    sb_init(&sb, 256);
    sb_append(&sb, "{\"ok\":false,\"error\":");
    sb_append_json_str(&sb, msg);
    sb_append(&sb, "}");
    enviar_json(sock, code, code == 400 ? "Bad Request" : (code == 403 ? "Forbidden" : "Error"), &sb);
    sb_free(&sb);
}

static const char *mime_por_extension(const char *path) {
    const char *ext = strrchr(path, '.');
    if (!ext) return "application/octet-stream";
    if (_stricmp(ext, ".html") == 0) return "text/html; charset=utf-8";
    if (_stricmp(ext, ".css") == 0)  return "text/css; charset=utf-8";
    if (_stricmp(ext, ".js") == 0)   return "application/javascript; charset=utf-8";
    if (_stricmp(ext, ".json") == 0) return "application/json; charset=utf-8";
    if (_stricmp(ext, ".jpg") == 0 || _stricmp(ext, ".jpeg") == 0) return "image/jpeg";
    if (_stricmp(ext, ".png") == 0)  return "image/png";
    if (_stricmp(ext, ".bmp") == 0)  return "image/bmp";
    if (_stricmp(ext, ".svg") == 0)  return "image/svg+xml";
    return "application/octet-stream";
}

static int es_ruta_segura(const char *rel) {
    if (!rel || rel[0] == '\0') return 0;
    if (strstr(rel, "..") != NULL) return 0;
    if (strchr(rel, '\\') != NULL) return 0;
    if (strchr(rel, ':') != NULL) return 0;
    return 1;
}

static void servir_archivo_estatico(SOCKET sock, const char *carpeta_base, const char *rel_path) {
    if (!es_ruta_segura(rel_path)) {
        enviar_json_error(sock, 403, "Ruta no permitida");
        return;
    }

    char ruta[512];
    snprintf(ruta, sizeof(ruta), "%s/%s", carpeta_base, rel_path);
    FILE *f = abrir_archivo_relativo(ruta, "rb", NULL, 0);
    if (!f) {
        const char *nf = "<h1>404 - Recurso no encontrado</h1>";
        enviar_respuesta(sock, 404, "Not Found", "text/html; charset=utf-8", nf, strlen(nf));
        return;
    }

    fseek(f, 0, SEEK_END);
    long tam = ftell(f);
    if (tam < 0) {
        fclose(f);
        enviar_json_error(sock, 500, "Error leyendo archivo");
        return;
    }
    rewind(f);

    char *buf = malloc((size_t)tam + 1);
    if (!buf) {
        fclose(f);
        enviar_json_error(sock, 500, "Memoria insuficiente");
        return;
    }
    size_t leidos = fread(buf, 1, (size_t)tam, f);
    fclose(f);

    enviar_respuesta(sock, 200, "OK", mime_por_extension(rel_path), buf, leidos);
    free(buf);
}

static void append_resultado_json(StrBuf *sb, const ResultadoECG *res) {
    sb_append(sb, "{");
    sb_printf(sb, "\"hay_arritmia\":%s,", res->hay_arritmia ? "true" : "false");
    sb_printf(sb, "\"tipo_diagnostico\":%d,", res->tipo_diagnostico);
    sb_printf(sb, "\"confianza\":%.6f,", res->confianza);
    sb_printf(sb, "\"nivel_urgencia\":%d,", res->nivel_urgencia);
    sb_append(sb, "\"urgencia_texto\":");
    sb_append_json_str(sb, obtener_texto_urgencia(res->nivel_urgencia));
    sb_append(sb, ",\"descripcion\":");
    sb_append_json_str(sb, res->descripcion);
    sb_append(sb, ",\"recomendacion\":");
    sb_append_json_str(sb, res->recomendacion);
    sb_append(sb, ",\"probabilidades\":[");
    for (int i = 0; i < NUM_CLASES_ECG; i++) {
        sb_printf(sb, "%.6f%s", res->probabilidades[i], (i == NUM_CLASES_ECG - 1) ? "" : ",");
    }
    sb_append(sb, "]}");
}

static void leer_ultimo_resumen_log(const char *ruta_log, EntrenamientoResumen *resumen, int *tiene) {
    *tiene = 0;
    FILE *f = abrir_archivo_relativo(ruta_log, "r", NULL, 0);
    if (!f) return;

    char linea[256];
    int count = 0;
    float err_ini = 0.0f, err_fin = 0.0f, acc_fin = 0.0f;
    while (fgets(linea, sizeof(linea), f)) {
        int ep = 0;
        float err = 0.0f, acc = 0.0f;
        if (sscanf(linea, "%d,%f,%f", &ep, &err, &acc) == 3) {
            if (count == 0) err_ini = err;
            err_fin = err;
            acc_fin = acc;
            count++;
        }
    }
    fclose(f);

    if (count > 0) {
        resumen->epocas_completadas = count;
        resumen->error_inicial = err_ini;
        resumen->error_final = err_fin;
        resumen->precision_final = acc_fin;
        *tiene = 1;
    }
}

static int asegurar_entrenamiento_red(redNeuronal *red, const char *dataset_path, const char *pesos_path,
                                      const char *log_path, EntrenamientoResumen *resumen, int *tiene_resumen) {
    if (cargar_pesos(red, pesos_path)) {
        leer_ultimo_resumen_log(log_path, resumen, tiene_resumen);
        return 1;
    }
    printf("  [IA] Pesos no encontrados en %s. Entrenando desde dataset...\n", pesos_path);
    datos *ds = cargar_dataset(dataset_path, red->topologia[0], red->topologia[red->nume_capas]);
    if (!ds) return 0;

    int ok = entrenar_red_con_log(red, ds->entradas, ds->salidas, ds->ejemplos, ds->num_salida, log_path, resumen);
    if (ok) {
        guardar_pesos(red, pesos_path);
        *tiene_resumen = 1;
    }
    liberarDatos(ds);
    return ok;
}

static void append_topologia_json(StrBuf *sb, const int *topo, int n) {
    sb_append(sb, "[");
    for (int i = 0; i < n; i++) {
        sb_printf(sb, "%d%s", topo[i], (i == n - 1) ? "" : ",");
    }
    sb_append(sb, "]");
}

static void handle_api_status(SOCKET sock, AppState *st) {
    EstadisticasCasos est = calcular_estadisticas_casos();
    StrBuf sb;
    sb_init(&sb, 1024);

    sb_append(&sb, "{\"ok\":true,\"red_bmp\":{");
    sb_printf(&sb, "\"entrenada\":%s,\"tasa\":%.4f,\"epocas\":%d,\"topologia\":",
              st->red_bmp->entrenada ? "true" : "false",
              st->cfg.tasa_bmp, st->cfg.epocas_bmp);
    append_topologia_json(&sb, st->cfg.topologia_bmp, st->cfg.num_capas_bmp);
    if (st->tiene_resumen_bmp) {
        sb_printf(&sb, ",\"error_inicial\":%.4f,\"error_final\":%.4f,\"precision_final\":%.2f",
                  st->resumen_bmp.error_inicial, st->resumen_bmp.error_final, st->resumen_bmp.precision_final);
    }
    sb_append(&sb, "},\"red_manual\":{");
    sb_printf(&sb, "\"entrenada\":%s,\"tasa\":%.4f,\"epocas\":%d,\"topologia\":",
              st->red_manual->entrenada ? "true" : "false",
              st->cfg.tasa_manual, st->cfg.epocas_manual);
    append_topologia_json(&sb, st->cfg.topologia_manual, st->cfg.num_capas_manual);
    if (st->tiene_resumen_manual) {
        sb_printf(&sb, ",\"error_inicial\":%.4f,\"error_final\":%.4f,\"precision_final\":%.2f",
                  st->resumen_manual.error_inicial, st->resumen_manual.error_final, st->resumen_manual.precision_final);
    }
    sb_append(&sb, "},\"estadisticas\":{");
    sb_printf(&sb, "\"total\":%d,\"verificados\":%d,\"pendientes\":%d,\"descartados\":%d,\"corregidos\":%d",
              est.total, est.verificados, est.pendientes, est.descartados, est.corregidos);
    sb_append(&sb, "}}");

    enviar_json(sock, 200, "OK", &sb);
    sb_free(&sb);
}

static void ejecutar_analisis_bmp_buffer(SOCKET sock, AppState *st, const unsigned char *bmp_bytes, size_t bmp_len,
                                         const Paciente *pac) {
    if (!st->red_bmp->entrenada) {
        enviar_json_error(sock, 400, "La Red BMP aun no esta entrenada");
        return;
    }

    int ancho = 0, alto = 0;
    unsigned char *pixeles = cargar_bmp_grises_mem(bmp_bytes, bmp_len, &ancho, &alto);
    if (!pixeles) {
        enviar_json_error(sock, 400, "El archivo no es un BMP valido de 24 o 32 bits sin compresion");
        return;
    }

    int inputs = st->red_bmp->topologia[0];
    int lado = 32;
    unsigned char *redi = redimensionar_grises(pixeles, ancho, alto, lado, lado);
    free(pixeles);
    if (!redi) {
        enviar_json_error(sock, 500, "Error redimensionando imagen BMP");
        return;
    }

    float *entradas = malloc((size_t)inputs * sizeof(float));
    if (!entradas) {
        free(redi);
        enviar_json_error(sock, 500, "Memoria insuficiente");
        return;
    }
    preparar_entradas_bmp(redi, entradas, inputs);

    Caso caso;
    memset(&caso, 0, sizeof(Caso));
    caso.paciente = *pac;
    caso.usa_bmp = 1;
    caso.resultado = clasificar(st->red_bmp, entradas);
    caso.tipo_correcto = caso.resultado.tipo_diagnostico;

    /* Reflejar la matriz normalizada exacta que recibe la red (trazo brillante) */
    for (int i = 0; i < lado * lado; i++) {
        int val = (int)(entradas[i] * 255.0f + 0.5f);
        if (val < 0) val = 0;
        if (val > 255) val = 255;
        redi[i] = (unsigned char)val;
    }
    free(entradas);

    guardar_caso_clinico(&caso);

    Caso *lista = NULL;
    int total_casos = 0;
    cargar_todos_casos(&lista, &total_casos);
    free(lista);
    int nuevo_indice = total_casos > 0 ? total_casos - 1 : 0;

    StrBuf sb;
    sb_init(&sb, 8192);
    sb_append(&sb, "{\"ok\":true,");
    sb_printf(&sb, "\"case_index\":%d,\"ancho_orig\":%d,\"alto_orig\":%d,\"resultado\":",
              nuevo_indice, ancho, alto);
    append_resultado_json(&sb, &caso.resultado);
    sb_append(&sb, ",\"preview_32x32\":[");
    for (int i = 0; i < lado * lado; i++) {
        sb_printf(&sb, "%d%s", (int)redi[i], (i == lado * lado - 1) ? "" : ",");
    }
    sb_append(&sb, "]}");

    free(redi);
    enviar_json(sock, 200, "OK", &sb);
    sb_free(&sb);
}

static void rellenar_paciente_por_defecto(Paciente *p) {
    if (p->nombre[0] == '\0') snprintf(p->nombre, sizeof(p->nombre), "Paciente Demo");
    if (p->cedula[0] == '\0') snprintf(p->cedula, sizeof(p->cedula), "10000000");
    if (p->medico[0] == '\0') snprintf(p->medico, sizeof(p->medico), "Dr. Sistema");
    if (p->fecha[0] == '\0') snprintf(p->fecha, sizeof(p->fecha), "13/11/2026");
}

static void handle_api_analyze_bmp(SOCKET sock, AppState *st, const char *query,
                                   const char *body, size_t body_len) {
    if (!body || body_len < 54) {
        enviar_json_error(sock, 400, "Debe adjuntar un archivo BMP valido");
        return;
    }

    Paciente pac;
    memset(&pac, 0, sizeof(pac));
    obtener_query_param(query, "nombre", pac.nombre, sizeof(pac.nombre));
    obtener_query_param(query, "cedula", pac.cedula, sizeof(pac.cedula));
    obtener_query_param(query, "medico", pac.medico, sizeof(pac.medico));
    obtener_query_param(query, "fecha", pac.fecha, sizeof(pac.fecha));
    rellenar_paciente_por_defecto(&pac);

    ejecutar_analisis_bmp_buffer(sock, st, (const unsigned char *)body, body_len, &pac);
}

static void handle_api_analyze_sample(SOCKET sock, AppState *st, const char *body) {
    if (!body) {
        enviar_json_error(sock, 400, "Cuerpo JSON requerido");
        return;
    }

    char muestra[128];
    if (!json_get_string(body, "sample", muestra, sizeof(muestra)) || !es_ruta_segura(muestra) ||
        strchr(muestra, '/') != NULL) {
        enviar_json_error(sock, 400, "Nombre de muestra invalido");
        return;
    }

    Paciente pac;
    memset(&pac, 0, sizeof(pac));
    json_get_string(body, "nombre", pac.nombre, sizeof(pac.nombre));
    json_get_string(body, "cedula", pac.cedula, sizeof(pac.cedula));
    json_get_string(body, "medico", pac.medico, sizeof(pac.medico));
    json_get_string(body, "fecha", pac.fecha, sizeof(pac.fecha));
    rellenar_paciente_por_defecto(&pac);

    char ruta[256];
    snprintf(ruta, sizeof(ruta), "samples/%s", muestra);
    FILE *f = abrir_archivo_relativo(ruta, "rb", NULL, 0);
    if (!f) {
        enviar_json_error(sock, 404, "Muestra BMP no encontrada");
        return;
    }

    fseek(f, 0, SEEK_END);
    long tam = ftell(f);
    if (tam <= 0 || tam > MAX_BODY_BYTES) {
        fclose(f);
        enviar_json_error(sock, 400, "Tamano de muestra invalido");
        return;
    }
    rewind(f);

    unsigned char *buf = malloc((size_t)tam);
    if (!buf) {
        fclose(f);
        enviar_json_error(sock, 500, "Memoria insuficiente");
        return;
    }
    size_t leidos = fread(buf, 1, (size_t)tam, f);
    fclose(f);

    ejecutar_analisis_bmp_buffer(sock, st, buf, leidos, &pac);
    free(buf);
}

static void handle_api_analyze_manual(SOCKET sock, AppState *st, const char *body) {
    if (!body) {
        enviar_json_error(sock, 400, "Cuerpo JSON requerido");
        return;
    }
    if (!st->red_manual->entrenada) {
        enviar_json_error(sock, 400, "La Red Manual aun no esta entrenada");
        return;
    }

    Paciente pac;
    memset(&pac, 0, sizeof(pac));
    json_get_string(body, "nombre", pac.nombre, sizeof(pac.nombre));
    json_get_string(body, "cedula", pac.cedula, sizeof(pac.cedula));
    json_get_string(body, "medico", pac.medico, sizeof(pac.medico));
    json_get_string(body, "fecha", pac.fecha, sizeof(pac.fecha));
    rellenar_paciente_por_defecto(&pac);

    double rr = 0, pr = 0, qrs = 0, qt = 0, bpm = 0;
    if (!json_get_double(body, "rr", &rr) || !json_get_double(body, "pr", &pr) ||
        !json_get_double(body, "qrs", &qrs) || !json_get_double(body, "qt", &qt) ||
        !json_get_double(body, "bpm", &bpm)) {
        enviar_json_error(sock, 400, "Faltan variables clinicas (rr, pr, qrs, qt, bpm)");
        return;
    }

    Caso caso;
    memset(&caso, 0, sizeof(Caso));
    caso.paciente = pac;
    caso.usa_bmp = 0;
    caso.intervalos_manuales[0] = (float)rr;
    caso.intervalos_manuales[1] = (float)pr;
    caso.intervalos_manuales[2] = (float)qrs;
    caso.intervalos_manuales[3] = (float)qt;
    caso.intervalos_manuales[4] = (float)bpm;

    float norm[5];
    normalizar_variables_clinicas(caso.intervalos_manuales, norm);
    caso.resultado = clasificar(st->red_manual, norm);
    caso.tipo_correcto = caso.resultado.tipo_diagnostico;

    guardar_caso_clinico(&caso);

    Caso *lista = NULL;
    int total_casos = 0;
    cargar_todos_casos(&lista, &total_casos);
    free(lista);
    int nuevo_indice = total_casos > 0 ? total_casos - 1 : 0;

    StrBuf sb;
    sb_init(&sb, 1024);
    sb_append(&sb, "{\"ok\":true,");
    sb_printf(&sb, "\"case_index\":%d,\"normalizados\":[%.4f,%.4f,%.4f,%.4f,%.4f],\"resultado\":",
              nuevo_indice, norm[0], norm[1], norm[2], norm[3], norm[4]);
    append_resultado_json(&sb, &caso.resultado);
    sb_append(&sb, "}");

    enviar_json(sock, 200, "OK", &sb);
    sb_free(&sb);
}

static void handle_api_cases(SOCKET sock) {
    Caso *lista = NULL;
    int cantidad = 0;
    if (!cargar_todos_casos(&lista, &cantidad)) {
        enviar_json_error(sock, 500, "No se pudieron leer los casos");
        return;
    }

    StrBuf sb;
    sb_init(&sb, 4096);
    sb_append(&sb, "{\"ok\":true,\"cases\":[");
    for (int i = 0; i < cantidad; i++) {
        const Caso *c = &lista[i];
        int tipo_final = c->diagnostico_corregido ? c->tipo_correcto : c->resultado.tipo_diagnostico;
        sb_append(&sb, "{");
        sb_printf(&sb, "\"index\":%d,", i);
        sb_append(&sb, "\"nombre\":"); sb_append_json_str(&sb, c->paciente.nombre);
        sb_append(&sb, ",\"cedula\":"); sb_append_json_str(&sb, c->paciente.cedula);
        sb_append(&sb, ",\"medico\":"); sb_append_json_str(&sb, c->paciente.medico);
        sb_append(&sb, ",\"fecha\":"); sb_append_json_str(&sb, c->paciente.fecha);
        sb_printf(&sb, ",\"tipo_diagnostico\":%d,", c->resultado.tipo_diagnostico);
        sb_append(&sb, "\"descripcion_ia\":"); sb_append_json_str(&sb, obtener_descripcion(c->resultado.tipo_diagnostico));
        sb_printf(&sb, ",\"tipo_correcto\":%d,", c->tipo_correcto);
        sb_append(&sb, "\"descripcion_final\":"); sb_append_json_str(&sb, obtener_descripcion(tipo_final));
        sb_printf(&sb, ",\"verificado\":%s,", c->verificado ? "true" : "false");
        sb_printf(&sb, "\"descartado\":%s,", c->descartado ? "true" : "false");
        sb_printf(&sb, "\"diagnostico_corregido\":%s,", c->diagnostico_corregido ? "true" : "false");
        sb_printf(&sb, "\"confianza\":%.4f,", c->resultado.confianza);
        sb_printf(&sb, "\"usa_bmp\":%s", c->usa_bmp ? "true" : "false");
        sb_append(&sb, "}");
        if (i < cantidad - 1) sb_append(&sb, ",");
    }
    sb_append(&sb, "]}");

    free(lista);
    enviar_json(sock, 200, "OK", &sb);
    sb_free(&sb);
}

static void handle_api_cases_verify(SOCKET sock, const char *body) {
    if (!body) {
        enviar_json_error(sock, 400, "Cuerpo JSON requerido");
        return;
    }

    double idx = -1, verif = 0, desc = 0, corr = 0, tipo_corr = 0;
    if (!json_get_double(body, "index", &idx)) {
        enviar_json_error(sock, 400, "Indice de caso requerido");
        return;
    }
    json_get_double(body, "verificado", &verif);
    json_get_double(body, "descartado", &desc);
    json_get_double(body, "corregido", &corr);
    json_get_double(body, "tipo_correcto", &tipo_corr);

    if (!actualizar_estado_caso((int)idx, (int)verif, (int)desc, (int)corr, (int)tipo_corr)) {
        enviar_json_error(sock, 400, "No se pudo actualizar el caso");
        return;
    }

    StrBuf sb;
    sb_init(&sb, 64);
    sb_append(&sb, "{\"ok\":true}");
    enviar_json(sock, 200, "OK", &sb);
    sb_free(&sb);
}

static void handle_api_stats(SOCKET sock) {
    EstadisticasCasos est = calcular_estadisticas_casos();
    StrBuf sb;
    sb_init(&sb, 512);
    sb_printf(&sb,
              "{\"ok\":true,\"total\":%d,\"verificados\":%d,\"pendientes\":%d,"
              "\"descartados\":%d,\"corregidos\":%d,\"por_tipo\":[%d,%d,%d,%d]}",
              est.total, est.verificados, est.pendientes, est.descartados, est.corregidos,
              est.por_tipo[0], est.por_tipo[1], est.por_tipo[2], est.por_tipo[3]);
    enviar_json(sock, 200, "OK", &sb);
    sb_free(&sb);
}

static void handle_api_report(SOCKET sock, const char *query) {
    char cedula[32];
    obtener_query_param(query, "cedula", cedula, sizeof(cedula));
    if (cedula[0] == '\0') {
        enviar_json_error(sock, 400, "Cedula requerida");
        return;
    }

    char *rep = leer_reporte_paciente(cedula);
    if (!rep) {
        enviar_json_error(sock, 404, "No existe reporte para esa cedula");
        return;
    }

    StrBuf sb;
    sb_init(&sb, strlen(rep) + 128);
    sb_append(&sb, "{\"ok\":true,\"cedula\":");
    sb_append_json_str(&sb, cedula);
    sb_append(&sb, ",\"reporte\":");
    sb_append_json_str(&sb, rep);
    sb_append(&sb, "}");
    free(rep);

    enviar_json(sock, 200, "OK", &sb);
    sb_free(&sb);
}

static void handle_api_samples(SOCKET sock) {
    StrBuf sb;
    sb_init(&sb, 512);
    sb_append(&sb, "{\"ok\":true,\"samples\":[");

    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA("samples\\*.bmp", &fd);
    if (hFind == INVALID_HANDLE_VALUE) {
        hFind = FindFirstFileA("..\\samples\\*.bmp", &fd);
    }

    int first = 1;
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                if (!first) sb_append(&sb, ",");
                sb_append_json_str(&sb, fd.cFileName);
                first = 0;
            }
        } while (FindNextFileA(hFind, &fd));
        FindClose(hFind);
    }

    sb_append(&sb, "]}");
    enviar_json(sock, 200, "OK", &sb);
    sb_free(&sb);
}

static void append_training_csv_json(StrBuf *sb, const char *ruta) {
    sb_append(sb, "[");
    FILE *f = abrir_archivo_relativo(ruta, "r", NULL, 0);
    if (f) {
        char linea[128];
        int first = 1;
        while (fgets(linea, sizeof(linea), f)) {
            int ep = 0;
            float err = 0.0f, acc = 0.0f;
            if (sscanf(linea, "%d,%f,%f", &ep, &err, &acc) == 3) {
                if (!first) sb_append(sb, ",");
                sb_printf(sb, "{\"epoch\":%d,\"loss\":%.6f,\"accuracy\":%.2f}", ep, err, acc);
                first = 0;
            }
        }
        fclose(f);
    }
    sb_append(sb, "]");
}

static void handle_api_training_log(SOCKET sock) {
    StrBuf sb;
    sb_init(&sb, 8192);
    sb_append(&sb, "{\"ok\":true,\"bmp\":");
    append_training_csv_json(&sb, "reports/training_log_bmp.csv");
    sb_append(&sb, ",\"manual\":");
    append_training_csv_json(&sb, "reports/training_log_manual.csv");
    sb_append(&sb, "}");
    enviar_json(sock, 200, "OK", &sb);
    sb_free(&sb);
}

static void handle_api_train(SOCKET sock, AppState *st, const char *body) {
    char target[32] = "both";
    char pin[64] = "";
    if (body) {
        json_get_string(body, "target", target, sizeof(target));
        json_get_string(body, "pin", pin, sizeof(pin));
    }

    if (strcmp(pin, st->cfg.admin_pin) != 0) {
        enviar_json_error(sock, 403, "PIN de administrador incorrecto");
        return;
    }

    int train_bmp = (strcmp(target, "bmp") == 0 || strcmp(target, "both") == 0);
    int train_manual = (strcmp(target, "manual") == 0 || strcmp(target, "both") == 0);

    if (train_bmp) {
        datos *ds = cargar_dataset(st->cfg.dataset_bmp, st->cfg.topologia_bmp[0],
                                   st->cfg.topologia_bmp[st->cfg.num_capas_bmp - 1]);
        if (ds) {
            reinicializar_pesos(st->red_bmp);
            if (entrenar_red_con_log(st->red_bmp, ds->entradas, ds->salidas, ds->ejemplos, ds->num_salida,
                                     "reports/training_log_bmp.csv", &st->resumen_bmp)) {
                guardar_pesos(st->red_bmp, st->cfg.pesos_bmp);
                st->tiene_resumen_bmp = 1;
            }
            liberarDatos(ds);
        }
    }

    if (train_manual) {
        datos *ds = cargar_dataset(st->cfg.dataset_manual, st->cfg.topologia_manual[0],
                                   st->cfg.topologia_manual[st->cfg.num_capas_manual - 1]);
        if (ds) {
            reinicializar_pesos(st->red_manual);
            if (entrenar_red_con_log(st->red_manual, ds->entradas, ds->salidas, ds->ejemplos, ds->num_salida,
                                     "reports/training_log_manual.csv", &st->resumen_manual)) {
                guardar_pesos(st->red_manual, st->cfg.pesos_manual);
                st->tiene_resumen_manual = 1;
            }
            liberarDatos(ds);
        }
    }

    handle_api_status(sock, st);
}

/* Lee una peticion HTTP completa (encabezados + cuerpo segun Content-Length) */
static char *leer_peticion_http(SOCKET sock, size_t *total_len_out, size_t *header_len_out) {
    size_t cap = RECV_CHUNK;
    size_t len = 0;
    char *buf = malloc(cap);
    if (!buf) return NULL;

    size_t header_end = 0;
    size_t content_length = 0;
    int headers_listos = 0;

    while (1) {
        if (len + 4096 > cap) {
            if (cap >= MAX_BODY_BYTES + 65536) {
                free(buf);
                return NULL;
            }
            size_t nueva = cap * 2;
            if (nueva > MAX_BODY_BYTES + 65536) nueva = MAX_BODY_BYTES + 65536;
            char *nb = realloc(buf, nueva);
            if (!nb) {
                free(buf);
                return NULL;
            }
            buf = nb;
            cap = nueva;
        }

        int r = recv(sock, buf + len, (int)(cap - len - 1), 0);
        if (r <= 0) break;
        len += (size_t)r;
        buf[len] = '\0';

        if (!headers_listos) {
            char *sep = strstr(buf, "\r\n\r\n");
            if (sep) {
                headers_listos = 1;
                header_end = (size_t)(sep - buf) + 4;

                /* Buscar Content-Length insensible a mayusculas */
                for (const char *p = buf; p < sep; ) {
                    const char *eol = strstr(p, "\r\n");
                    if (!eol || eol > sep) eol = sep;
                    if (_strnicmp(p, "Content-Length:", 15) == 0) {
                        content_length = (size_t)strtoull(p + 15, NULL, 10);
                    }
                    p = eol + 2;
                }
                if (content_length > MAX_BODY_BYTES) {
                    free(buf);
                    return NULL;
                }
            }
        }

        if (headers_listos && len >= header_end + content_length) {
            break;
        }
    }

    if (!headers_listos) {
        free(buf);
        return NULL;
    }

    *total_len_out = len;
    *header_len_out = header_end;
    return buf;
}

static void procesar_cliente(SOCKET sock, AppState *st) {
    size_t total_len = 0, header_len = 0;
    char *req = leer_peticion_http(sock, &total_len, &header_len);
    if (!req) {
        closesocket(sock);
        return;
    }

    char metodo[16] = {0};
    char url[1024] = {0};
    if (sscanf(req, "%15s %1023s", metodo, url) != 2) {
        free(req);
        closesocket(sock);
        return;
    }

    char path[1024] = {0};
    char query[1024] = {0};
    char *qmark = strchr(url, '?');
    if (qmark) {
        size_t plen = (size_t)(qmark - url);
        if (plen >= sizeof(path)) plen = sizeof(path) - 1;
        memcpy(path, url, plen);
        path[plen] = '\0';
        snprintf(query, sizeof(query), "%s", qmark + 1);
    } else {
        snprintf(path, sizeof(path), "%s", url);
    }

    const char *body = req + header_len;
    size_t body_len = (total_len > header_len) ? (total_len - header_len) : 0;

    if (strcmp(metodo, "GET") == 0 && strcmp(path, "/") == 0) {
        servir_archivo_estatico(sock, "web", "index.html");
    } else if (strcmp(metodo, "GET") == 0 && strcmp(path, "/api/status") == 0) {
        handle_api_status(sock, st);
    } else if (strcmp(metodo, "GET") == 0 && strcmp(path, "/api/cases") == 0) {
        handle_api_cases(sock);
    } else if (strcmp(metodo, "GET") == 0 && strcmp(path, "/api/stats") == 0) {
        handle_api_stats(sock);
    } else if (strcmp(metodo, "GET") == 0 && strcmp(path, "/api/report") == 0) {
        handle_api_report(sock, query);
    } else if (strcmp(metodo, "GET") == 0 && strcmp(path, "/api/samples") == 0) {
        handle_api_samples(sock);
    } else if (strcmp(metodo, "GET") == 0 && strcmp(path, "/api/training-log") == 0) {
        handle_api_training_log(sock);
    } else if (strcmp(metodo, "POST") == 0 && strcmp(path, "/api/analyze/bmp") == 0) {
        handle_api_analyze_bmp(sock, st, query, body, body_len);
    } else if (strcmp(metodo, "POST") == 0 && strcmp(path, "/api/analyze/sample") == 0) {
        handle_api_analyze_sample(sock, st, body);
    } else if (strcmp(metodo, "POST") == 0 && strcmp(path, "/api/analyze/manual") == 0) {
        handle_api_analyze_manual(sock, st, body);
    } else if (strcmp(metodo, "POST") == 0 && strcmp(path, "/api/cases/verify") == 0) {
        handle_api_cases_verify(sock, body);
    } else if (strcmp(metodo, "POST") == 0 && strcmp(path, "/api/train") == 0) {
        handle_api_train(sock, st, body);
    } else if (strcmp(metodo, "POST") == 0 && strcmp(path, "/api/shutdown") == 0) {
        StrBuf sb;
        sb_init(&sb, 32);
        sb_append(&sb, "{\"ok\":true}");
        enviar_json(sock, 200, "OK", &sb);
        sb_free(&sb);
        st->ejecutando = 0;
    } else if (strcmp(metodo, "GET") == 0 && strncmp(path, "/samples/", 9) == 0) {
        servir_archivo_estatico(sock, "samples", path + 9);
    } else if (strcmp(metodo, "GET") == 0 && path[0] == '/') {
        servir_archivo_estatico(sock, "web", path + 1);
    } else {
        enviar_json_error(sock, 404, "Endpoint no encontrado");
    }

    free(req);
    closesocket(sock);
}

static void lanzar_ventana_app(int puerto) {
    char url[128];
    snprintf(url, sizeof(url), "http://127.0.0.1:%d", puerto);

    const char *candidatos[] = {
        "C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe",
        "C:\\Program Files\\Microsoft\\Edge\\Application\\msedge.exe",
        "C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe"
    };

    for (size_t i = 0; i < sizeof(candidatos) / sizeof(candidatos[0]); i++) {
        DWORD attrs = GetFileAttributesA(candidatos[i]);
        if (attrs != INVALID_FILE_ATTRIBUTES) {
            char cmd[512];
            snprintf(cmd, sizeof(cmd),
                     "\"%s\" --app=%s --window-size=1380,860 --window-position=80,40",
                     candidatos[i], url);
            STARTUPINFOA si;
            PROCESS_INFORMATION pi;
            ZeroMemory(&si, sizeof(si));
            ZeroMemory(&pi, sizeof(pi));
            si.cb = sizeof(si);
            if (CreateProcessA(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
                CloseHandle(pi.hProcess);
                CloseHandle(pi.hThread);
                return;
            }
        }
    }

    ShellExecuteA(NULL, "open", url, NULL, NULL, SW_SHOWNORMAL);
}

int main(int argc, char **argv) {
    SetConsoleOutputCP(CP_UTF8);

    int abrir_navegador = 1;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--no-browser") == 0) {
            abrir_navegador = 0;
        }
    }

    printf("=====================================================\n");
    printf("   SINA-VISUAL PRO | Motor Clinico IA en C\n");
    printf("=====================================================\n");

    AppState st;
    memset(&st, 0, sizeof(st));
    st.cfg = cargar_config("data/config.txt");
    st.ejecutando = 1;

    st.red_bmp = inicializar_red(st.cfg.num_capas_bmp, st.cfg.topologia_bmp,
                                 st.cfg.tasa_bmp, st.cfg.epocas_bmp);
    st.red_manual = inicializar_red(st.cfg.num_capas_manual, st.cfg.topologia_manual,
                                    st.cfg.tasa_manual, st.cfg.epocas_manual);

    if (!st.red_bmp || !st.red_manual) {
        fprintf(stderr, "[ERROR] No se pudieron inicializar las redes neuronales.\n");
        return 1;
    }

    printf("[1/3] Cargando Red Neuronal BMP...\n");
    asegurar_entrenamiento_red(st.red_bmp, st.cfg.dataset_bmp, st.cfg.pesos_bmp,
                               "reports/training_log_bmp.csv", &st.resumen_bmp, &st.tiene_resumen_bmp);

    printf("[2/3] Cargando Red Neuronal Clinica Manual...\n");
    asegurar_entrenamiento_red(st.red_manual, st.cfg.dataset_manual, st.cfg.pesos_manual,
                               "reports/training_log_manual.csv", &st.resumen_manual, &st.tiene_resumen_manual);

    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        fprintf(stderr, "[ERROR] Error inicializando WinSock.\n");
        liberar_red(st.red_bmp);
        liberar_red(st.red_manual);
        return 1;
    }

    SOCKET server_sock = INVALID_SOCKET;
    int puerto = st.cfg.puerto_servidor;
    for (int intento = 0; intento < 10; intento++) {
        server_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (server_sock == INVALID_SOCKET) continue;

        int opt = 1;
        setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof(opt));

        struct sockaddr_in addr;
        ZeroMemory(&addr, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = htons((unsigned short)(puerto + intento));

        if (bind(server_sock, (struct sockaddr *)&addr, sizeof(addr)) == 0 &&
            listen(server_sock, SOMAXCONN) == 0) {
            puerto = puerto + intento;
            break;
        }
        closesocket(server_sock);
        server_sock = INVALID_SOCKET;
    }

    if (server_sock == INVALID_SOCKET) {
        fprintf(stderr, "[ERROR] No se pudo abrir un puerto local.\n");
        WSACleanup();
        liberar_red(st.red_bmp);
        liberar_red(st.red_manual);
        return 1;
    }

    printf("[3/3] Servidor activo en http://127.0.0.1:%d\n", puerto);
    printf("      Cierra esta ventana o usa 'Apagar Servidor' para salir.\n");
    fflush(stdout);

    if (abrir_navegador) {
        lanzar_ventana_app(puerto);
    }

    while (st.ejecutando) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(server_sock, &fds);

        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 250000;

        int ready = select(0, &fds, NULL, NULL, &tv);
        if (ready > 0 && FD_ISSET(server_sock, &fds)) {
            SOCKET client = accept(server_sock, NULL, NULL);
            if (client != INVALID_SOCKET) {
                procesar_cliente(client, &st);
            }
        }
    }

    closesocket(server_sock);
    WSACleanup();
    liberar_red(st.red_bmp);
    liberar_red(st.red_manual);
    printf("Servidor cerrado correctamente.\n");
    return 0;
}

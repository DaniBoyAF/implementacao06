/*
 * banker.c - Algoritmo de Banker
 *
 * Baseado em Silberschatz, A. et al. Operating System Concepts, 10a ed., p. P-45.
 * Diferenca em relacao ao livro: o numero de clientes e o de recursos nao sao fixos.
 *
 * Uso:      ./banker <instancias do recurso 0> <instancias do recurso 1> ...
 * Entradas: customer.txt  maximo de cada cliente (uma linha por cliente, valores separados por virgula)
 *           commands.txt  comandos RQ, RL e *
 * Saida:    result.txt
 *
 * Em caso de erro (argumentos ou arquivos invalidos) o programa escreve uma mensagem em
 * stderr e encerra com status 1, sem gerar o result.txt.
 */
#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#define ARQ_CLIENTES  "customer.txt"
#define ARQ_COMANDOS  "commands.txt"
#define ARQ_RESULTADO "result.txt"

/* Largura das duas primeiras colunas da tabela impressa pelo comando '*' */
#define LARG_MAXIMUM    10
#define LARG_ALLOCATION 12

/* Linhas nao vazias de um arquivo, com o numero da linha original (para mensagens de erro) */
typedef struct {
    char **txt;
    int   *num;
    int    n;
} Linhas;

/* Estado do sistema: matrizes n x r guardadas em vetores lineares (indice i * r + j) */
typedef struct {
    int  n;            /* numero de clientes */
    int  r;            /* numero de tipos de recurso */
    int *disponivel;   /* available[r] */
    int *maximo;       /* maximum[n][r] */
    int *alocado;      /* allocation[n][r] */
    int *necessidade;  /* need[n][r] = maximum - allocation */
} Estado;

typedef enum { CMD_RQ, CMD_RL, CMD_ESTADO } TipoCmd;

typedef struct {
    TipoCmd tipo;
    int     cliente;
    int     linha;     /* linha em commands.txt */
    int    *valores;   /* r valores (RQ e RL) */
} Comando;

static Linhas   g_lin_cli, g_lin_cmd;
static Estado   g_e;
static Comando *g_cmds;
static int      g_ncmds;
static FILE    *g_out;

/* ------------------------------------------------------------------ utilitarios */

static void liberar_linhas(Linhas *l)
{
    for (int i = 0; i < l->n; i++)
        free(l->txt[i]);
    free(l->txt);
    free(l->num);
    memset(l, 0, sizeof *l);
}

static void liberar(void)
{
    liberar_linhas(&g_lin_cli);
    liberar_linhas(&g_lin_cmd);
    free(g_e.disponivel);
    free(g_e.maximo);
    free(g_e.alocado);
    free(g_e.necessidade);
    for (int i = 0; i < g_ncmds; i++)
        free(g_cmds[i].valores);
    free(g_cmds);
    if (g_out)
        fclose(g_out);
}

static void erro(const char *fmt, ...)
{
    va_list ap;

    fputs("Erro: ", stderr);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    exit(EXIT_FAILURE);
}

static void *reservar(size_t n, size_t tam)
{
    void *p = calloc(n ? n : 1, tam);

    if (!p)
        erro("memoria insuficiente");
    return p;
}

/* Remove espacos no inicio e no fim (inclui \r e \n). Devolve ponteiro para dentro de s. */
static char *aparar(char *s)
{
    char *fim;

    while (isspace((unsigned char)*s))
        s++;
    fim = s + strlen(s);
    while (fim > s && isspace((unsigned char)fim[-1]))
        *--fim = '\0';
    return s;
}

/* Aceita apenas inteiros decimais nao negativos (somente digitos) que caibam em int. */
static int inteiro_nao_negativo(const char *s, int *saida)
{
    char *fim;
    long  v;

    if (*s == '\0')
        return 0;
    for (const char *p = s; *p; p++)
        if (!isdigit((unsigned char)*p))
            return 0;
    errno = 0;
    v = strtol(s, &fim, 10);
    if (errno != 0 || *fim != '\0' || v > INT_MAX)
        return 0;
    *saida = (int)v;
    return 1;
}

static int menor_ou_igual(const int *a, const int *b, int r)
{
    for (int j = 0; j < r; j++)
        if (a[j] > b[j])
            return 0;
    return 1;
}

/* ------------------------------------------------------------------ leitura */

/* Le o arquivo guardando as linhas nao vazias. Linha em branco so e aceita no fim do arquivo. */
static void ler_arquivo(const char *caminho, Linhas *l)
{
    FILE   *f = fopen(caminho, "r");
    char   *buf = NULL;
    size_t  cap = 0;
    int     num = 0, branco = 0, cap_l = 0, falha;

    if (!f)
        erro("nao foi possivel abrir '%s': %s", caminho, strerror(errno));

    while (getline(&buf, &cap, f) != -1) {
        char *s = aparar(buf);

        num++;
        if (*s == '\0') {
            if (!branco)
                branco = num;
            continue;
        }
        if (branco) {
            free(buf);
            fclose(f);
            erro("'%s' linha %d: linha em branco no meio do arquivo", caminho, branco);
        }
        if (l->n == cap_l) {
            int    novo = cap_l ? cap_l * 2 : 16;
            char **t = realloc(l->txt, (size_t)novo * sizeof *t);
            int   *u;

            if (!t)
                erro("memoria insuficiente");
            l->txt = t;
            u = realloc(l->num, (size_t)novo * sizeof *u);
            if (!u)
                erro("memoria insuficiente");
            l->num = u;
            cap_l = novo;
        }
        l->txt[l->n] = strdup(s);
        if (!l->txt[l->n])
            erro("memoria insuficiente");
        l->num[l->n] = num;
        l->n++;
    }
    falha = ferror(f);
    free(buf);
    fclose(f);
    if (falha)
        erro("falha ao ler '%s'", caminho);
    if (l->n == 0)
        erro("'%s' esta vazio", caminho);
}

/* customer.txt: n linhas, cada uma com r inteiros separados por virgula. */
static void ler_clientes(int r)
{
    int colunas = 1;

    ler_arquivo(ARQ_CLIENTES, &g_lin_cli);

    for (const char *p = g_lin_cli.txt[0]; *p; p++)
        if (*p == ',')
            colunas++;
    if (colunas != r)
        erro("numero de argumentos (%d) incompativel com %s (%d recursos por linha)",
             r, ARQ_CLIENTES, colunas);

    g_e.n = g_lin_cli.n;
    g_e.r = r;
    g_e.maximo      = reservar((size_t)g_e.n * r, sizeof(int));
    g_e.alocado     = reservar((size_t)g_e.n * r, sizeof(int));
    g_e.necessidade = reservar((size_t)g_e.n * r, sizeof(int));

    for (int i = 0; i < g_e.n; i++) {
        char *p = g_lin_cli.txt[i];

        for (int j = 0; j < r; j++) {
            char *virg = strchr(p, ',');
            char *tok;
            int   v;

            if (j < r - 1) {
                if (!virg)
                    erro("%s linha %d: esperados %d valores separados por virgula",
                         ARQ_CLIENTES, g_lin_cli.num[i], r);
                *virg = '\0';
            } else if (virg) {
                erro("%s linha %d: esperados %d valores separados por virgula",
                     ARQ_CLIENTES, g_lin_cli.num[i], r);
            }
            tok = aparar(p);
            if (!inteiro_nao_negativo(tok, &v))
                erro("%s linha %d: valor invalido '%s' (esperado inteiro nao negativo)",
                     ARQ_CLIENTES, g_lin_cli.num[i], tok);
            g_e.maximo[i * r + j]      = v;
            g_e.necessidade[i * r + j] = v;
            p = virg ? virg + 1 : p;
        }
    }
}

static int contar_tokens(const char *linha)
{
    char *copia = strdup(linha), *salva, *t;
    int   n = 0;

    if (!copia)
        erro("memoria insuficiente");
    for (t = strtok_r(copia, " \t", &salva); t; t = strtok_r(NULL, " \t", &salva))
        n++;
    free(copia);
    return n;
}

/* commands.txt: "RQ i v1 ... vr", "RL i v1 ... vr" ou "*". Valida o arquivo inteiro antes de executar. */
static void ler_comandos(int r)
{
    ler_arquivo(ARQ_COMANDOS, &g_lin_cmd);
    g_cmds = reservar((size_t)g_lin_cmd.n, sizeof *g_cmds);

    for (int k = 0; k < g_lin_cmd.n; k++) {
        char    *linha = g_lin_cmd.txt[k], *salva, *t;
        int      num = g_lin_cmd.num[k], ntok = contar_tokens(linha), v;
        Comando *c = &g_cmds[g_ncmds];

        t = strtok_r(linha, " \t", &salva);
        c->linha = num;

        if (strcmp(t, "*") == 0) {
            if (ntok != 1)
                erro("%s linha %d: o comando '*' nao recebe parametros", ARQ_COMANDOS, num);
            c->tipo = CMD_ESTADO;
            g_ncmds++;
            continue;
        }
        if (strcmp(t, "RQ") == 0)
            c->tipo = CMD_RQ;
        else if (strcmp(t, "RL") == 0)
            c->tipo = CMD_RL;
        else
            erro("%s linha %d: comando desconhecido '%s' (esperado RQ, RL ou *)",
                 ARQ_COMANDOS, num, t);

        if (ntok != r + 2)
            erro("%s linha %d: esperados o numero do cliente e %d valores de recurso "
                 "(um por argumento), encontrados %d parametros",
                 ARQ_COMANDOS, num, r, ntok - 1);

        t = strtok_r(NULL, " \t", &salva);
        if (!inteiro_nao_negativo(t, &v) || v >= g_e.n)
            erro("%s linha %d: cliente invalido '%s' (esperado de 0 a %d)",
                 ARQ_COMANDOS, num, t, g_e.n - 1);
        c->cliente = v;

        c->valores = reservar((size_t)r, sizeof(int));
        g_ncmds++;                       /* ja registrado: liberar() cuida de c->valores */
        for (int j = 0; j < r; j++) {
            t = strtok_r(NULL, " \t", &salva);
            if (!inteiro_nao_negativo(t, &v))
                erro("%s linha %d: valor invalido '%s' (esperado inteiro nao negativo)",
                     ARQ_COMANDOS, num, t);
            c->valores[j] = v;
        }
    }
}

/* ------------------------------------------------------------------ algoritmo de Banker */

/* Algoritmo de seguranca (livro, secao 8.6.3.1). */
static int estado_seguro(const Estado *e)
{
    int *work = reservar((size_t)e->r, sizeof(int));
    int *fim = reservar((size_t)e->n, sizeof(int));
    int  terminados = 0, progresso = 1;

    memcpy(work, e->disponivel, (size_t)e->r * sizeof(int));

    while (progresso && terminados < e->n) {
        progresso = 0;
        for (int i = 0; i < e->n; i++) {
            if (fim[i] || !menor_ou_igual(&e->necessidade[i * e->r], work, e->r))
                continue;
            for (int j = 0; j < e->r; j++)
                work[j] += e->alocado[i * e->r + j];
            fim[i] = 1;
            terminados++;
            progresso = 1;
        }
    }
    free(work);
    free(fim);
    return terminados == e->n;
}

/* Soma (sinal = +1) ou subtrai (sinal = -1) req de alocado[i], e faz o inverso em necessidade e disponivel. */
static void aplicar(Estado *e, int i, const int *req, int sinal)
{
    for (int j = 0; j < e->r; j++) {
        e->disponivel[j]            -= sinal * req[j];
        e->alocado[i * e->r + j]    += sinal * req[j];
        e->necessidade[i * e->r + j] -= sinal * req[j];
    }
}

/* ------------------------------------------------------------------ saida */

static void imprimir_vetor(FILE *f, const int *v, int r)
{
    for (int j = 0; j < r; j++)
        fprintf(f, j ? " %d" : "%d", v[j]);
}

/* Imprime "a b c " e completa com espacos ate a largura indicada. */
static void imprimir_celula(FILE *f, const int *v, int r, int largura)
{
    int escritos = 0;

    for (int j = 0; j < r; j++)
        escritos += fprintf(f, "%d ", v[j]);
    for (; escritos < largura; escritos++)
        fputc(' ', f);
}

static void imprimir_estado(FILE *f, const Estado *e)
{
    fprintf(f, "MAXIMUM   |   ALLOCATION  |   NEED\n");
    for (int i = 0; i < e->n; i++) {
        imprimir_celula(f, &e->maximo[i * e->r], e->r, LARG_MAXIMUM);
        fprintf(f, "|   ");
        imprimir_celula(f, &e->alocado[i * e->r], e->r, LARG_ALLOCATION);
        fprintf(f, "|   ");
        imprimir_celula(f, &e->necessidade[i * e->r], e->r, 0);
        fputc('\n', f);
    }
    fprintf(f, "AVAILABLE ");
    imprimir_celula(f, e->disponivel, e->r, 0);
    fputc('\n', f);
}

/* ------------------------------------------------------------------ execucao */

static void pedir(FILE *f, Estado *e, const Comando *c)
{
    int *necessidade = &e->necessidade[c->cliente * e->r];

    if (!menor_ou_igual(c->valores, necessidade, e->r)) {
        fprintf(f, "The customer %d request ", c->cliente);
        imprimir_vetor(f, c->valores, e->r);
        fprintf(f, " was denied because exceed its maximum allocation\n");
    } else if (!menor_ou_igual(c->valores, e->disponivel, e->r)) {
        fprintf(f, "The resources ");
        imprimir_vetor(f, e->disponivel, e->r);
        fprintf(f, " are not enough to customer %d request ", c->cliente);
        imprimir_vetor(f, c->valores, e->r);
        fprintf(f, " \n");              /* o result.txt do professor termina esta linha com um espaco */
    } else {
        aplicar(e, c->cliente, c->valores, +1);
        if (estado_seguro(e)) {
            fprintf(f, "Allocate to customer %d the resources ", c->cliente);
            imprimir_vetor(f, c->valores, e->r);
            fputc('\n', f);
        } else {
            aplicar(e, c->cliente, c->valores, -1);
            fprintf(f, "The customer %d request ", c->cliente);
            imprimir_vetor(f, c->valores, e->r);
            fprintf(f, " was denied because result in an unsafe state\n");
        }
    }
}

static void liberar_recursos(FILE *f, Estado *e, const Comando *c)
{
    const int *alocado = &e->alocado[c->cliente * e->r];

    if (!menor_ou_igual(c->valores, alocado, e->r)) {
        fprintf(f, "The customer %d release ", c->cliente);
        imprimir_vetor(f, c->valores, e->r);
        fprintf(f, " was denied because exceed its allocation\n");
        return;
    }
    aplicar(e, c->cliente, c->valores, -1);
    fprintf(f, "Release from customer %d the resources ", c->cliente);
    imprimir_vetor(f, c->valores, e->r);
    fputc('\n', f);
}

static void executar(void)
{
    g_out = fopen(ARQ_RESULTADO, "w");
    if (!g_out)
        erro("nao foi possivel criar '%s': %s", ARQ_RESULTADO, strerror(errno));

    for (int k = 0; k < g_ncmds; k++) {
        const Comando *c = &g_cmds[k];

        if (c->tipo == CMD_RQ)
            pedir(g_out, &g_e, c);
        else if (c->tipo == CMD_RL)
            liberar_recursos(g_out, &g_e, c);
        else
            imprimir_estado(g_out, &g_e);
    }
    if (fclose(g_out) != 0) {
        g_out = NULL;
        erro("falha ao gravar '%s'", ARQ_RESULTADO);
    }
    g_out = NULL;
}

/* ------------------------------------------------------------------ main */

int main(int argc, char **argv)
{
    int r = argc - 1;

    atexit(liberar);

    if (r < 1)
        erro("numero de argumentos insuficiente. Uso: ./banker <instancias do recurso 0> "
             "[<instancias do recurso 1> ...]");

    g_e.r = r;
    g_e.disponivel = reservar((size_t)r, sizeof(int));
    for (int j = 0; j < r; j++)
        if (!inteiro_nao_negativo(argv[j + 1], &g_e.disponivel[j]))
            erro("argumento %d invalido '%s' (esperado inteiro nao negativo)", j + 1, argv[j + 1]);

    ler_clientes(r);
    ler_comandos(r);
    executar();

    printf("%d comandos processados. Saida gravada em %s\n", g_ncmds, ARQ_RESULTADO);
    return EXIT_SUCCESS;
}

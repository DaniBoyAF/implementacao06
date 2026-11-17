#!/usr/bin/env bash
# Gera o evidencias.log (compilacao, testes, verificacao cruzada, valgrind e sanitizers).
# Uso (na raiz do repositorio):  bash testes/gerar_evidencias.sh
cd "$(dirname "$0")/.." || exit 1
LOG=evidencias.log
{
    echo "evidencias.log - Algoritmo de Banker"
    echo "Gerado em: $(date '+%d/%m/%Y %H:%M:%S %Z')"
    echo "Sistema:   $(uname -sr)"
    echo "Compilador: $(gcc --version | head -1)"
    echo "Make:       $(make --version | head -1)"

    echo; echo "################ 1. Compilacao (make clean; make) ################"
    make clean && make
    echo "status: $?"

    echo; echo "################ 2. Testes (testes/rodar_testes.sh) ################"
    bash testes/rodar_testes.sh 2>&1
    echo "status dos testes: $?"

    echo; echo "################ 3. Verificacao cruzada com implementacao independente em Python ################"
    echo "(a referencia testa todas as ordens de termino dos clientes, sem reutilizar o algoritmo do banker.c)"
    python3 -I testes/verificar_aleatorio.py 2000 2026
    python3 -I testes/verificar_aleatorio.py 2000 7

    echo; echo "################ 4. Valgrind (vazamentos e acessos invalidos) ################"
    if command -v valgrind >/dev/null; then
        T="$(mktemp -d)"
        cp testes/exemplo/customer.txt testes/exemplo/commands.txt "$T/"
        B="$PWD/banker"
        echo "\$ valgrind --leak-check=full --error-exitcode=9 ./banker 10 5 7   (exemplo do enunciado)"
        ( cd "$T" && valgrind --leak-check=full --show-leak-kinds=all --error-exitcode=9 "$B" 10 5 7 2>&1 | grep -E "All heap blocks|ERROR SUMMARY|in use at exit|total heap usage" ); echo "status: ${PIPESTATUS[0]}"
        echo "\$ valgrind ... ./banker 10 5   (argumentos incompativeis)"
        ( cd "$T" && valgrind --leak-check=full --error-exitcode=9 "$B" 10 5 2>&1 | grep -E "^Erro|All heap blocks|ERROR SUMMARY" )
        printf 'RQ 0 1 1 1\nRQ 9 1 1 1\n' > "$T/commands.txt"
        echo "\$ valgrind ... ./banker 10 5 7   (commands.txt com cliente inexistente)"
        ( cd "$T" && valgrind --leak-check=full --error-exitcode=9 "$B" 10 5 7 2>&1 | grep -E "^Erro|All heap blocks|ERROR SUMMARY" )
        rm -rf "$T"
    else
        echo "valgrind nao disponivel neste ambiente."
    fi

    echo; echo "################ 5. AddressSanitizer + UndefinedBehaviorSanitizer ################"
    gcc -std=c11 -Wall -Wextra -Wpedantic -g -fsanitize=address,undefined -fno-omit-frame-pointer -o /tmp/banker_san banker.c && {
        T="$(mktemp -d)"; cp testes/exemplo/customer.txt testes/exemplo/commands.txt "$T/"
        ( cd "$T" && /tmp/banker_san 10 5 7 && diff result.txt "$OLDPWD/testes/exemplo/result_esperado.txt" >/dev/null && echo "exemplo do enunciado: sem erros de sanitizer e result.txt identico ao esperado" )
        ( cd "$T" && /tmp/banker_san 10 5 >/dev/null 2>"$T/err"; echo "erro de argumentos: status $? (sem relatorio de sanitizer: $(grep -c Sanitizer "$T/err"))" )
        rm -rf "$T" /tmp/banker_san
    }

    echo; echo "################ 6. Recompilacao final e limpeza ################"
    make clean && make && ls -l banker && make clean && ls banker 2>&1 | head -1
} > "$LOG" 2>&1
echo "Evidencias gravadas em $LOG"

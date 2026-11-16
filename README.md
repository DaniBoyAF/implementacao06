# Algoritmo de Banker

Implementação em C do algoritmo de Banker (Silberschatz et al., *Operating System Concepts*, 10ª ed., p. P-45) para a disciplina de Infraestrutura de Software, CESAR School. O número de clientes e o de tipos de recurso não são fixos: vêm dos arquivos de entrada e dos argumentos do programa.

Autor: Daniel Andrade Ferreira Silva (`dafs`).

## Requisitos

- Linux, Unix ou macOS
- `gcc` e `make` (código em C11, usa `getline` e `strtok_r` do POSIX)

## Compilar e executar

```bash
make                 # gera o executável ./banker
./banker 10 5 7      # 3 tipos de recurso, com 10, 5 e 7 instâncias
make clean           # remove o executável
```

Cada argumento é o número de instâncias de um tipo de recurso, então a quantidade de argumentos define a quantidade de tipos de recurso. O programa lê `customer.txt` e `commands.txt` **do diretório atual** e grava `result.txt` nele.

## Arquivos de entrada

**`customer.txt`**: uma linha por cliente, com o máximo que ele pode pedir de cada recurso, separado por vírgula. O número de linhas é o número de clientes.

```
7,5,3
3,2,2
9,0,2
2,2,2
4,3,3
```

**`commands.txt`**: um comando por linha.

| Comando | Significado |
|---|---|
| `RQ i a b c` | cliente `i` solicita `a b c` instâncias de cada recurso |
| `RL i a b c` | cliente `i` libera `a b c` instâncias de cada recurso |
| `*` | imprime as matrizes MAXIMUM, ALLOCATION, NEED e o vetor AVAILABLE |

## Saída (`result.txt`)

| Situação | Linha gravada |
|---|---|
| RQ atendido (estado continua seguro) | `Allocate to customer i the resources ...` |
| RL | `Release from customer i the resources ...` |
| RQ maior que o que o cliente ainda pode pedir (`need`) | `The customer i request ... was denied because exceed its maximum allocation` |
| RQ maior que o disponível | `The resources <available> are not enough to customer i request ... ` |
| RQ que levaria a estado inseguro (o pedido é desfeito) | `The customer i request ... was denied because result in an unsafe state` |
| RL maior que o alocado (caso não definido no enunciado) | `The customer i release ... was denied because exceed its allocation` |
| `*` | tabela `MAXIMUM \| ALLOCATION \| NEED` e `AVAILABLE ...` |

Para o exemplo do enunciado (`./banker 10 5 7`), o `result.txt` gerado é **idêntico byte a byte** ao `result.txt` fornecido pelo professor, inclusive os espaços no fim de algumas linhas.

## Como o programa decide

Para cada `RQ`, na ordem:

1. Se o pedido é maior que `need` do cliente, nega (excede o máximo declarado).
2. Se o pedido é maior que `available`, nega (recursos insuficientes).
3. Caso contrário, simula a alocação e executa o **algoritmo de segurança**. Se existe uma sequência em que todos os clientes conseguem terminar, mantém a alocação. Senão, desfaz a simulação e nega.

## Tratamento de erros

Os erros abaixo imprimem `Erro: ...` em `stderr`, encerram com status 1 e **não geram `result.txt`**. O `commands.txt` é validado por inteiro antes de qualquer comando ser executado, então nunca sobra um resultado parcial.

| Erro | Exemplo |
|---|---|
| Nenhum argumento | `./banker` |
| Argumento que não é inteiro não negativo | `./banker 10 a 7`, `./banker 10 -5 7` |
| Número de argumentos diferente do número de recursos de `customer.txt` | `./banker 10 5` |
| Arquivo inexistente, vazio, sem permissão ou que não é arquivo regular | `customer.txt` ausente |
| `customer.txt` com valor inválido, colunas a mais ou a menos, campo vazio, linha em branco no meio | `7,x,3`, `7,5`, `7,,3` |
| `commands.txt` com comando desconhecido (só `RQ`, `RL`, `*` em maiúsculas) | `XX 0 1 1 1` |
| `RQ`/`RL` com quantidade de valores diferente do número de argumentos | `RQ 0 1 1` |
| Cliente inexistente ou negativo | `RQ 9 1 1 1`, `RQ -1 1 1 1` |
| Valor negativo ou não numérico, `*` com parâmetros, linha em branco no meio | `RL 0 1 -1 1` |

Linhas em branco **no fim** dos arquivos e finais de linha `\r\n` são aceitos, assim como espaços ao redor dos valores.

## Decisões de projeto

- `RL` maior que o alocado não está no enunciado. O programa nega com uma mensagem e mantém o estado, em vez de deixar `allocation` negativo.
- Não se exige que o máximo declarado seja menor ou igual ao total de instâncias. Um cliente nessa situação nunca consegue terminar, então qualquer pedido dele é negado como estado inseguro.
- Em caso de erro de formato o programa encerra em vez de ignorar a linha, como pede o enunciado.

## Estrutura do repositório

```
banker.c                    código-fonte
Makefile                    compilação (make) e limpeza (make clean)
README.md                   este arquivo
evidencias.log              evidências dos testes (gerado por testes/gerar_evidencias.sh)
relatorio/dafs.pdf          relatório
testes/exemplo/             customer.txt, commands.txt e result.txt do enunciado
testes/rodar_testes.sh      35 testes: exemplo, entradas válidas e todos os casos de erro
testes/verificar_aleatorio.py   verificação cruzada com implementação independente (Python)
testes/gerar_evidencias.sh  gera o evidencias.log
```

## Testes

```bash
make
bash testes/rodar_testes.sh            # exemplo do professor + casos de erro
python3 testes/verificar_aleatorio.py  # 500 casos aleatórios contra a referência em Python
bash testes/gerar_evidencias.sh        # regenera o evidencias.log (inclui valgrind e sanitizers)
```

A referência em Python decide se um estado é seguro testando **todas as ordens possíveis de término** dos clientes, sem reutilizar o algoritmo do `banker.c`, e a comparação com o `result.txt` é byte a byte.

# void-shell

Mini-shell educacional escrito em C, desenvolvido passo a passo para
aprender sistemas operacionais e programação de baixo nível.

## Funcionalidades

- REPL com prompt `void-shell>`
- Builtins: `cd`, `pwd`, `exit`, `echo`, `clear`, `jobs`, `fg`
- Execução de comandos externos (`fork`/`execvp`/`waitpid`)
- Redireções: `>`, `>>`, `<`
- Pipes: `|`
- Aspas: `'...'` e `"..."`
- Variáveis: `$VAR`, `$?`
- Sequência e condicionais: `;`, `&&`, `||`
- Background: `comando &`, `jobs`, `fg`
- Histórico persistente com setas e Tab (readline)
- Globbing: `*`, `?`, `[...]`
- Expansão de `~`
- Galáxia e banner animados no topo (região de rolagem própria, sem conflitar com o readline)
- `/kalc`: manual interativo de matemática básica (soma, subtração,
  multiplicação e divisão), com menu clicável e navegação por teclado
- `/lang`: troca o idioma do shell entre **English** e
  **Português (Brasil)**; a escolha fica salva para as próximas sessões
  (padrão: English)

## Build

```sh
make
./void-shell
```

## Roadmap

- v8: variáveis de shell e `export`
- v9: subshell e grupos
- v10: job control completo (Ctrl+Z, `bg`)

## Licença

MIT

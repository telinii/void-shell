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
- `Ctrl+C` cancela a linha atual sem encerrar o shell (inclusive com
  sugestão ativa); `Ctrl+D` sai
- Sugestões inline estilo fish: texto cinza completa o comando a partir do
  histórico, comandos internos, atalhos e executáveis do `PATH`, e completa
  caminhos/arquivos nos argumentos (`cd Doc` → `Documents/`, `ls ma` →
  `main.c`); **→** aceita a sugestão
- Globbing: `*`, `?`, `[...]`
- Expansão de `~`
- Galáxia em espiral com rotação lenta e banner animados no topo (região de rolagem própria, sem conflitar com o readline)
- Limpa a tela e o scrollback do terminal ao iniciar
- `/kalc`: manual interativo de matemática básica (soma, subtração,
  multiplicação e divisão), com menu clicável e navegação por teclado
- `/lang`: troca o idioma do shell entre **English** e
  **Português (Brasil)**; a escolha fica salva para as próximas sessões
  (padrão: English)
- Guia rápido de boas-vindas ao abrir o shell; `/tutorial` (ou `/help`)
  mostra o guia de novo
- `/theme`: temas de cores (**Galaxy**, **Matrix**, **Synthwave**, **Mono**,
  **Ocean**, **Sunset**, **Dracula**, **Nord**, **Amber**)
  com preview no menu; a escolha fica salva para as próximas sessões
- `/theme` também tem a coluna **void-term bg color**, que muda o fundo do
  terminal (via OSC 11) independente do tema das letras: **Void**,
  **Black**, **Midnight**, **Slate**, **Deep Sea**, **Plum**, **Ember**,
  **Forest** e **Wine**. `Tab`/`←`/`→` alterna a coluna; também aceita
  `/theme bg <nome|#rrggbb>`; a escolha fica salva em
  `~/.void_shell_term_bg`
- `Ctrl+P`: command palette com busca fuzzy em builtins, `/comandos`,
  histórico e executáveis do `PATH`; **Enter** executa, **Tab** edita no
  prompt, **Esc** fecha
- HUD fixa na última linha: diretório atual (`~`), branch do git com `*`
  quando há alterações, status e duração do último comando (`ok`/`err`),
  jobs em background, idioma, hora, uso de CPU e RAM (lidos de `/proc`)

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

# void-term

Terminal nativo em **C + GTK4 + VTE**, feito para rodar o `void-shell`
com a mesma identidade visual. Faz parte da branch `with-terminal` deste
repositório; o `void-shell` sozinho fica na branch `main`.

Todo o chrome (header, abas, bordas, scrollbar, menu, busca, cursor e
seleção) é derivado da cor de fundo escolhida em `/theme` →
`void-term bg color`, no estilo do kitty.

## Recursos

- **Abas** com botão de fechar e título dinâmico
- **Splits** horizontais e verticais, com painel focado destacado
- **Busca no scrollback** com destaque, próxima/anterior e Esc para fechar
- **Scrollback** de 10.000 linhas
- **Barra de título e botões próprios** (minimizar, maximizar, fechar)
- **Menu** com atalhos para abas, splits, busca, fonte e cópia/cola
- **Chrome derivado do fundo**: header, abas, bordas, scrollbar, menu,
  busca, cursor e seleção seguem a cor de fundo (estilo kitty)
- **Fundo do terminal dinâmico**: segue a cor escolhida em `/theme` →
  `void-term bg color` no `void-shell` (OSC 11)
- Roda o `void-shell` embutido (ao lado do executável); senão
  `~/void-shell/void-shell`; senão `$SHELL`
- `Ctrl+C` no terminal envia SIGINT para o processo filho (comportamento normal)

## Dependências

- GTK4
- VTE para GTK4 (`vte4` no Arch/CachyOS)
- Compilador C e `pkg-config`

No Arch/CachyOS:

```sh
sudo pacman -S gtk4 vte4
```

## Compilar e rodar

```sh
cd term
make
./void-term
```

Ou, na raiz do repositório, para compilar o shell e o terminal juntos:

```sh
make bundle
./void-term
```

Se `vte4` ainda não estiver instalado no sistema, o Makefile usa uma cópia
local extraída em `.deps/` (apenas para desenvolvimento). Para a instalação
normal, instale o `vte4` e rode `make clean && make`.

## Instalar

```sh
make install
```

Isso copia o binário para `~/.local/bin/void-term` e o atalho para
`~/.local/share/applications/void-term.desktop`. O terminal também aparece
no menu de aplicativos.

## Atalhos

| Atalho | Ação |
| --- | --- |
| `Ctrl+Shift+T` / `Ctrl+Shift+N` | Nova aba |
| `Ctrl+Shift+W` | Fechar aba |
| `Ctrl+Shift+D` | Dividir à direita |
| `Ctrl+Shift+E` | Dividir abaixo |
| `Ctrl+Shift+X` | Fechar painel |
| `Ctrl+Shift+F` | Buscar no scrollback |
| `Ctrl+Shift+M` | Abrir/fechar o menu |
| `Ctrl+Tab` / `Ctrl+Shift+Tab` | Próxima / aba anterior |
| `Ctrl+Shift+C` / `Ctrl+Shift+V` | Copiar / colar |
| `Ctrl+=` / `Ctrl+-` | Aumentar / diminuir fonte |
| `Ctrl+0` | Fonte padrão |
| `Ctrl+Shift+Q` | Sair |

Na busca: `Enter` vai para a próxima ocorrência, `Shift+Enter` para a
anterior e `Esc` fecha.

## Variáveis de ambiente

| Variável | Efeito |
| --- | --- |
| `VOID_TERM_SHELL` | Comando inicial (ex.: `VOID_TERM_SHELL="bash"`) |
| `VOID_TERM_FONT` | Fonte e tamanho (ex.: `VOID_TERM_FONT="JetBrains Mono 12"`) |

## Estrutura

```
src/main.c          aplicação, abas, splits, busca e CSS
Makefile            build e instalação
void-term.desktop   atalho para o menu de aplicativos
```

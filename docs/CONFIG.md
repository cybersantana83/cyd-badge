# Referência do config.json

O painel web grava tudo em `/badge/config.json` no cartão SD. Esta página é para quem quer editar à mão ou entender o formato. A pasta `sd_card/` do repositório tem um exemplo.

## Estrutura

```json
{
  "settings": { ... },
  "pages": [ { "type": "profile", ... }, { "type": "qr", ... } ]
}
```

### settings

| Campo | Padrão | Descrição |
|---|---|---|
| `rotation` | `1` | 0 a 3. 1 e 3 são paisagem (320x240), 0 e 2 retrato (240x320) |
| `brightness` | `180` | 0 a 255 |
| `invert_colors` | `false` | Corrige painéis com cores invertidas |
| `touch_invert_x` | `false` | Espelha o lado do toque |
| `slideshow_seconds` | `0` | Troca de página automática (0 desliga) |
| `show_dots` | `true` | Indicador de páginas no rodapé |
| `led_follow_accent` | `false` | LED RGB da placa acompanha a cor de destaque da página |
| `bg`, `fg`, `muted`, `accent` | tema escuro | Cores padrão em `#RRGGBB` |
| `font_big`, `font_medium`, `font_small` | embutidas | Opcional: fontes `.vlw` no SD para trocar o visual |

As fontes Poppins (com acentos) já vêm embutidas no firmware.

### Cores por página

Toda página aceita `bg`, `fg`, `muted` e `accent` para sobrescrever o tema, e `hide_dots` para esconder o indicador.

Outras opções aceitas por qualquer página:

| Campo | Descrição |
|---|---|
| `boost` | `true` deixa o brilho no máximo enquanto a página está na tela (útil para QR lido por leitores) |
| `layout` | Em páginas `qr`, `big` aumenta o QR para quase toda a largura em retrato |

### Tipos de página

**`profile`**: foto + nome

| Campo | Descrição |
|---|---|
| `name`, `title`, `subtitle` | Textos (nome e cargo em até 2 linhas, subtítulo em até 3) |
| `image` | Ex.: `/badge/img/avatar.jpg` (JPG, PNG ou BMP) |
| `image_px` | Lado da imagem salva no SD, em pixels (o painel envia 160). A placa redimensiona para o layout |
| `image_shape` | `circle` ou `square` |
| `layout` | `center` (foto em cima) ou `side` (foto à esquerda, só em paisagem) |

Sem foto, aparece um círculo com as iniciais.

**`qr`**: QR code de qualquer link

| Campo | Descrição |
|---|---|
| `label` | Título no cabeçalho colorido (ex.: LinkedIn) |
| `url` | Conteúdo do QR (ou `data` para texto livre, Wi-Fi etc.) |
| `handle` | Texto em destaque ao lado do QR |
| `note` | Texto menor de apoio |
| `icon` | Opcional, PNG 48x48 acima do handle |
| `header_fg` | Cor do texto do cabeçalho (padrão branco) |

Dica: dá para gerar QR de Wi-Fi com `"data": "WIFI:T:WPA;S:MinhaRede;P:senha;;"`.

**Credencial de evento**: é uma página `qr` com o conteúdo do crachá em `data`, `layout: "big"` e `boost: true`. O painel monta isso sozinho a partir da foto do crachá. O clique duplo no BOOT vai para a primeira página com `"kind": "event"` ou `"boost": true`.

```json
{ "type": "qr", "kind": "event", "layout": "big", "boost": true,
  "label": "IT Summit 2026", "handle": "Seu Nome", "note": "Empresa", "data": "CONTEUDO_DO_QR" }
```

**`vcard`**: QR que salva o contato direto na agenda (formato MECARD)

Campos do QR: `name`, `org`, `phone`, `email`, `url`, `note_card`. Campos visuais iguais ao `qr` (`label`, `handle`, `note`, `icon`).

**`image`**: imagem em tela cheia

`image` (320x240 em paisagem) e `caption` opcional.

**`text`**: página de texto

`label` (cabeçalho), `title` e `body` (use `\n` para quebrar linha).

## Preparando imagens

O decodificador do ESP32 não redimensiona e não lê JPEG progressivo, então a imagem precisa ir no tamanho exato:

Isso só é necessário se você editar o config à mão; pelo painel o recorte é feito no celular.

```bash
uv run --with pillow tools/prepare_image.py minha_foto.jpg sd_card/badge/img/avatar.jpg --avatar 160
uv run --with pillow tools/prepare_image.py arte.png sd_card/badge/img/banner.jpg --screen
uv run --with pillow tools/prepare_image.py logo.png sd_card/badge/img/github.png --icon 48
```

## Fontes personalizadas

O firmware já traz a Poppins (licença OFL) embutida. Para usar outra fonte, gere os `.vlw`, copie para o SD e aponte `font_big`, `font_medium` e `font_small` no config:

```bash
uv run --with pillow tools/make_vlw.py MinhaFonte-Bold.ttf 26 sd_card/badge/fonts/big.vlw
uv run --with pillow tools/make_vlw.py MinhaFonte-Medium.ttf 18 sd_card/badge/fonts/medium.vlw
uv run --with pillow tools/make_vlw.py MinhaFonte-Regular.ttf 14 sd_card/badge/fonts/small.vlw
```

Use `--extra "★→"` para incluir símbolos além de ASCII + Latin-1.

## Pré-visualização no PC

Renderiza todas as páginas do `config.json` em PNG, com as mesmas fontes e o mesmo layout do firmware:

```bash
uv run --with pillow --with qrcode tools/preview.py sd_card --out preview
```


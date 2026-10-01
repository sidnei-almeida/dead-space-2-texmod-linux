# DS2TexInject

**Use pacotes de textura do TexMod (`.tpf`) no Dead Space 2 no Linux.**

Funciona com Steam + Proton / GE-Proton, DXVK, ReShade e Steam Deck.

🇺🇸 **[Read in English →](README.md)**

---

## O que é isso?

O Dead Space 2 tem ótimos mods de textura HD: trajes em 4K, armas e muito mais. Eles vêm em arquivos `.tpf`, feitos para o **TexMod** ou o **uMod**.

**O TexMod e o uMod não funcionam no Linux.** O DS2TexInject faz o papel deles. É um plugin pequeno que roda dentro do jogo, encontra as texturas que os seus pacotes substituem e faz a troca. Os `.tpf` que você já tem funcionam sem mudar nada.

---

## Resumo rápido

| Passo | O que fazer |
|:---:|---|
| 1 | Instalar o **MarkerPatch** na pasta do jogo. |
| 2 | Colocar uma **opção de inicialização** no Steam. |
| 3 | Colocar seus **pacotes `.tpf`** numa pasta `texmod`. |
| 4 | Rodar o **`./install.sh`** deste repositório. |
| 5 | **Jogar.** Aperte **F10** para comparar antes e depois. |

Cada passo está explicado em detalhe logo abaixo.

---

## Passo 1 — Instalar o MarkerPatch

O [MarkerPatch](https://github.com/Wemino/MarkerPatch) é um pacote de correções para o Dead Space 2. O DS2TexInject precisa dele porque é o MarkerPatch que carrega o plugin.

1. Baixe a versão mais recente em **https://github.com/Wemino/MarkerPatch/releases**.
2. Extraia na pasta do jogo, ao lado do `deadspace2.exe`. Os arquivos `dinput8.dll` e `MarkerPatch.ini` devem ficar lá.

> **Onde fica a pasta do jogo?** No Steam, clique com o botão direito em **Dead Space 2** → **Gerenciar** → **Explorar arquivos locais**.
> Normalmente é `~/.local/share/Steam/steamapps/common/Dead Space 2`.

## Passo 2 — Opção de inicialização no Steam

O Proton ignora as DLLs colocadas na pasta do jogo, a não ser que você mande ele carregar.

1. No Steam, clique com o botão direito em **Dead Space 2** → **Propriedades** → **Geral**.
2. Cole uma destas linhas em **Opções de inicialização**:

**Sem ReShade:**
```
WINEDLLOVERRIDES="dinput8=n,b" %command%
```

**Com ReShade** (tem um `d3d9.dll` na pasta do jogo):
```
WINEDLLOVERRIDES="d3d9,dinput8=n,b" %command%
```

## Passo 3 — Baixar os pacotes de textura

Os pacotes de textura **não vêm** neste repositório. Eles são dos autores, então baixe nas páginas originais.

Estes pacotes foram testados e funcionam:

| Pacote | Arquivos |
|---|---|
| [2K-4K Isaac Suits and Face](https://www.nexusmods.com/deadspace2/mods/82) | `1-4KMainSuitsnew.tpf`, `2Kaio.tpf` |
| [Return to Titan](https://www.nexusmods.com/deadspace2/mods/97) ([pacote de texturas](https://www.nexusmods.com/deadspace2/mods/40)) | `1WEP_RTTn.tpf`, `3…9INTERACTABLES_RTT*.tpf` |

Outros pacotes `.tpf` do Dead Space 2 também devem funcionar.

Crie uma pasta chamada **`texmod`** dentro da pasta do jogo e coloque todos os `.tpf` nela:

```
Dead Space 2/
├── deadspace2.exe
├── dinput8.dll            ← MarkerPatch
└── texmod/
    ├── 1-4KMainSuitsnew.tpf
    ├── 2Kaio.tpf
    └── ...
```

## Passo 4 — Instalar o DS2TexInject

Abra um terminal e rode:

```sh
git clone https://github.com/sidnei-almeida/ds2-texinject-linux
cd ds2-texinject-linux
./install.sh
```

O instalador faz tudo sozinho:

- acha a pasta do Dead Space 2 nas suas bibliotecas do Steam;
- compila o plugin (na primeira vez ele baixa um compilador, sem precisar de `sudo`);
- copia o plugin para `Dead Space 2/plugins/`;
- extrai e verifica todas as texturas dos seus `.tpf`.

Se ele não achar a pasta do jogo sozinho, passe o caminho:

```sh
./install.sh "/caminho/para/Dead Space 2"
```

No final deve aparecer algo assim:

```
396 texturas unicas (665 MB) em .../texmod/_cache
Nenhuma textura quebrada encontrada.
```

<details>
<summary><b>Instalação manual (sem compilar)</b></summary>

1. Baixe o `DS2TexInject.asi` e o `DS2TexInject.ini` na [página de Releases](https://github.com/sidnei-almeida/ds2-texinject-linux/releases).
2. Coloque o `DS2TexInject.asi` em `Dead Space 2/plugins/`. Crie a pasta `plugins` se ela não existir.
3. Coloque o `DS2TexInject.ini` em `Dead Space 2/`.
4. Extraia as texturas:
   ```sh
   python3 ds2tex.py "/caminho/para/Dead Space 2/texmod"
   ```
</details>

## Passo 5 — Jogar

Abra o Dead Space 2 pelo Steam, normalmente.

- Aperte **F10** durante o jogo para **ligar e desligar** as texturas novas e ver a diferença.
- Para confirmar que está funcionando, abra `Dead Space 2/DS2TexInject.log`. Cada linha `MATCH` é uma textura sendo trocada.

As texturas são trocadas conforme aparecem na tela. Os trajes e as armas aparecem quando você os pega no jogo.

---

## Adicionar ou remover pacotes

1. Coloque ou apague arquivos `.tpf` em `Dead Space 2/texmod/`.
2. Rode de novo:
   ```sh
   python3 ds2tex.py "/caminho/para/Dead Space 2/texmod"
   ```

**Quando dois pacotes mudam a mesma textura,** vale o pacote cujo nome vem primeiro em ordem alfabética. Para escolher a ordem, crie `texmod/load_order.txt` com um nome de arquivo por linha, o mais importante primeiro.

## Configurações — `DS2TexInject.ini`

O arquivo fica na pasta do jogo e abre em qualquer editor de texto.

| Opção | Padrão | O que faz |
|---|---|---|
| `Enabled` | `1` | `0` desliga a troca de texturas. |
| `ToggleKey` | `0x79` | Tecla que liga e desliga as texturas. `0x79` é o **F10**. |
| `Pool` | `managed` | Mude para `default` se o jogo fechar sozinho com muitos pacotes 4K. Gasta menos memória. |
| `LogHashes` | `0` | `1` registra no log todas as texturas que o jogo carrega. Serve para diagnóstico. |
| `DumpTextures` | `0` | `1` salva as texturas originais do jogo em `texmod/_dump/`. Serve para criar seus próprios pacotes. |
| `TextureDir` | `texmod\_cache` | Pasta onde ficam as texturas extraídas. |

---

## Problemas comuns

| Problema | Solução |
|---|---|
| **O arquivo `DS2TexInject.log` não aparece** | O plugin não está carregando. Confira se o `dinput8.dll` (MarkerPatch) está na pasta do jogo, se o `plugins/DS2TexInject.asi` existe e se a opção de inicialização do Passo 2 está configurada. |
| **O log não tem linhas `MATCH`** | Confira se você rodou o `ds2tex.py` e se a pasta `texmod/_cache/` tem arquivos. Jogue um pouco também: os trajes e as armas só aparecem mais adiante no jogo. |
| **O jogo fecha sozinho depois de um tempo** | O Dead Space 2 é um jogo de 32 bits e pode ficar sem memória com muitas texturas 4K. Coloque `Pool=default` no `DS2TexInject.ini` ou remova alguns pacotes. |
| **Alguma textura ficou estranha** | Aperte **F10** para confirmar que é o pacote que causa isso. Depois abra uma issue dizendo qual é o pacote. |
| **Qualquer outra coisa** | [Abra uma issue](https://github.com/sidnei-almeida/ds2-texinject-linux/issues) e anexe o `DS2TexInject.log`. |

---

## Como funciona

Você não precisa desta parte para usar o mod.

**Por que o TexMod não funciona no Linux:**

- O TexMod e o uMod se injetam no jogo com truques do Windows que o Wine não suporta.
- Eles também precisam substituir o `d3d9.dll`, um lugar que o ReShade e o DXVK já ocupam.
- Além disso, o `.exe` do jogo é protegido pela DRM da EA.

**O que o DS2TexInject faz no lugar:**

1. O MarkerPatch carrega o `DS2TexInject.asi` quando o jogo abre.
2. O plugin intercepta o `Direct3DCreate9` dentro do `d3d9.dll` que o jogo estiver usando (ReShade ou DXVK). A partir daí ele intercepta os métodos de device e de textura do Direct3D 9.
3. Quando o jogo envia uma textura, o plugin calcula o mesmo **hash CRC32 que o TexMod usa** (sobre o maior nível de mipmap).
4. Se existir `texmod/_cache/0x<hash>.dds` (ou `.png` / `.bmp`), o plugin carrega esse arquivo e desenha ele no lugar do original. As substituições só são carregadas quando aparecem e são liberadas quando o jogo deixa de usar.

**O que o `ds2tex.py` faz:**

Um arquivo `.tpf` é um zip codificado com XOR e protegido com a senha fixa do TexMod. O `ds2tex.py` decodifica, verifica cada textura (formato, tamanho, arquivo danificado) e salva em `texmod/_cache/` com o hash no nome.

**Para compilar você mesmo:**

```sh
./build.sh      # gera dist/DS2TexInject.asi
```

Ele usa o `i686-w64-mingw32-g++` se estiver instalado. Se não estiver, baixa o [llvm-mingw](https://github.com/mstorsjo/llvm-mingw) em `~/.local/opt`. Todo o código está em `src/ds2texinject.cpp`.

---

## Créditos

- **Pacotes de textura:** os respectivos autores no [Nexus Mods](https://www.nexusmods.com/deadspace2).
- **[MarkerPatch](https://github.com/Wemino/MarkerPatch)**, do Wemino, que carrega o plugin.
- A compatibilidade de hash segue o comportamento do **TexMod** e do **[uMod](https://code.google.com/archive/p/texmod/)**.

**Testado em:** Arch Linux, GE-Proton 11, DXVK, ReShade 6.8 e MarkerPatch.

## Licença

[MIT](LICENSE). Os pacotes de textura não fazem parte deste projeto e pertencem aos seus autores.

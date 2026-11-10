# SuperMetroid3DS

A Nintendo 3DS port of **Super Metroid** built via **static recompilation** using [SuperMetroidRecomp](https://github.com/mstan/SuperMetroidRecomp) by mstan.

> **No ROM is included and none will ever be.** See [Legal](#legal).

---

## Estado: Funciona no Emulador, Crashes no 3DS Real

⚠️ **O jogo NÃO funciona no 3DS real** — crashed na entrada (PC=0x07100000)

| Build | Azahar Emulator | 3DS Hardware |
|-------|-----------------|--------------|
| Test pattern (`SM_WITH_CORE=0`) | ✅ Estável (500+ frames, até 450 fps) | ❓ Não testado |
| Core embedido (`SM_WITH_CORE=1`) | 📊 Funciona (500 frames, 44-217 sys_fps) | ❌ **Crashes no entry point** |

**Crash Dump 3DS (hardware real):**
- PC = `0x07100000` (entry point, antes do `main()`)
- Causa: crash durante inicialização estática ou runtime C
- Causa raiz: T027 não resolvido — falta *fiber shim* para 3DS (contexto ARM11)

---

## Início Rápido

### O que é necessário
- 3DS com custom firmware (CFW)
- **Nota Importante:** O jogo **não funciona ainda no hardware 3DS real** (crash na entrada). Funciona no emulador Azahar.
- Homebrew Launcher (ou qualquer launcher `.3dsx`)
- ROM **Super Metroid (USA ou Japão)** legalmente obtida (`.sfc`)

### Instalação
1. Copie a pasta `SuperMetroid3DS` para o cartão SD:
   ```
   /3ds/SuperMetroid3DS/
   ```
2. Coloque o seu ficheiro ROM com um destes nomes:
   - `sm.sfc`
   - `SuperMetroid-usa.sfc`
   
   O executável verifica automaticamente o SHA-256 do ROM no arranque.

3. Lançar **SuperMetroid3DS** de Homebrew Launcher.

### Controles
| Botão 3DS | Ação SNES |
|-----------|-----------|
| A         | Saltar    |
| B         | Disparar / Correr |
| X         | Selecionar Item |
| Y         | Cancelar Item |
| L / R     | Visar Cima / Baixo |
| Start     | Start     |
| Select    | Select    |
| D-Pad     | Movimento |

---

## Route B: Decompilação snesrev/sm

Este port utiliza **Route B** — a [decompilação snesrev/sm](https://github.com/snesrev/sm) funcionalmente completa — em vez da pipeline original de recompilação.

### Resultados de Validação
| Métrica | Valor |
|---------|-------|
| Bancos de código | 32 |
| Funções | 5,671 |
| Instruções | 132,318 |
| Tamanho binário (x86_64) | 1.8 MB |
| Tamanho binário (3DSX, core embedido) | 33.9 MB |
| Precisão de frames (20,000 frames) | **0 mismatches** |
| PatchBytes workarounds | 90 |

Estudo completo: [`docs/ROUTE_B_STUDY.md`](docs/ROUTE_B_STUDY.md)

**Screenshots de demonstração:**
- [screenshot.png](screenshot.png) — Tela de título (frame 60, ~1.8% non-zero)
- [screenshot_gameplay.png](screenshot_gameplay.png) — Momento de jogabilidade (frame 1800, ~42% non-zero)

---

## Compilar a Partir do Código Fonte

### Pré-requisitos
- Docker / Podman
- ROM Super Metroid legalmente obtida (`.sfc`)
- UPSTREAM_ROOT apontando para um clone de [SuperMetroidRecomp](https://github.com/mstan/SuperMetroidRecomp)

### Build

```bash
# Clonar este repositório
git clone git@github.com:linuxkafe/SuperMetroid3DS-Recomp.git
cd SuperMetroid3DS-Recomp

# Definir path upstream (onde está clonado o SuperMetroidRecomp)
export UPSTREAM_ROOT=/path/to/SuperMetroidRecomp

# Build 3DS homebrew
make docker-build
```

**Opções de build:**

| Opção | Output | Tamanho | Estado |
|-------|--------|---------|--------|
| `SM_WITH_CORE=0` (default) | `build3ds/supermetroid3ds.3dsx` | ~180 KB | ✅ Estável no emulador |
| `SM_WITH_CORE=1` | `build3ds/supermetroid3ds.3dsx` | ~33.9 MB | ⚠️ Crashes no 3DS real |

**Performance (Azahar headless - 500 frames testados):**
| Build | sys_fps | emu_speed |
|-------|---------|-----------|
| SM_WITH_CORE=0 | 0 / 450 fps | 0.02x - 8.7x |
| SM_WITH_CORE=1 | 44 / 217 fps | 0.2x - 4.0x |

O core tem desempenho limitado pela falta de otimizações específicas para 3DS.

### Validação Headless (CI)
```bash
make docker-headless-test
```
Executa o harness comparativo — valida 0 mismatches em 20,000 frames.

---

## Estrutura do Projecto
```
SuperMetroid3DS-Recomp/
├── platform/3ds/          # Camada de plataforma 3DS (SDL -> 3DS shim)
│   ├── host.c             # Carregamento ROM, verificação SHA-256, SnesInit
│   └── host.h
├── frontend/3ds/          # Frontend 3DS (input, áudio, render, main)
│   ├── 3ds_main.c
│   ├── 3ds_input.c
│   ├── 3ds_audio.c
│   └── 3ds_render.c
├── docs/                  # Documentação
│   └── ROUTE_B_STUDY.md   # Estudo completo de viabilidade com dados medidos
├── scripts/               # Scripts de CI / validação
├── CMakeLists.txt
├── Makefile
└── README.md              # Este ficheiro
```

---

## Legal

Este é um **port não oficial**. Super Metroid é copyright pela Nintendo.

**Este repositório contém:**
- Nenhum ROM, nenhuma imagem ROM, nenhuma cópia de código da Nintendo
- Nenhum trabalho derivado que incorpore dados do ROM
- Apenas código shim original de plataforma e automação de build

**O utilizador deve:**
- Fornecer um ficheiro ROM Super Metroid `.sfc` legalmente obtido
- Verificar que o SHA-256 corresponde ao digest esperado (o app o verifica automaticamente)
- Cumprir as leis locais sobre propriedade de jogos e backups

Upstream: [SuperMetroidRecomp](https://github.com/mstan/SuperMetroidRecomp) — o digest esperado está em `rom_identity.txt`; `make doctor` imprime ambos para verificação.

---

## Créditos

- **mstan** — SuperMetroidRecomp, snesrecomp, decompilação snesrev/sm
- **snesrev team** — Decompilação funcionalmente completa de Super Metroid
- **devkitPro** — toolchain devkitARM e libctru
- **Super Metroid** — © 1994 Nintendo

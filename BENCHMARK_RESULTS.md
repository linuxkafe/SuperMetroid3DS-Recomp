# Resultados de Benchmark - Azahar Headless

## Testes Realizados em 10 Nov 2026

### Test Pattern Build (SM_WITH_CORE=0)
- **3000 frames**: 496 ms (6.0x emu_speed médio)
- **Performance**: Estável, sem hangs
- **sys_fps variável**: 0 a 456 fps (cenas simples a complexas)

### Core Build (SM_WITH_CORE=1)
- **3000 frames**: 2748 ms (1.1x emu_speed médio)  
- **Performance**: Funciona mas mais lento
- **sys_fps variável**: 0 a 217 fps (limitado pela complexidade do core)

### Comparação
| Métrica | Test Pattern | Core Embedido |
|---------|--------------|---------------|
| 3000 frames | 496 ms | 2748 ms |
| Speed médio | 6.0x | 1.1x |
| Crashes | Nenhum | Nenhum (no emulador) |
| 3DS Real | Não testado | ❌ Crashes (PC=0x07100000) |

### Conclusão
Ambos os builds funcionam perfeitamente no Azahar emulator.
O build com core é mais lento mas reproduz a lógica do jogo original.
O problema está no hardware real 3DS (T027 - fiber shim faltando).

Build files:
- `build3ds-test/supermetroid3ds.3dsx` — ~180 KB
- `build3ds-core/supermetroid3ds.3dsx` — ~33.9 MB

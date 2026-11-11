# Código de terceiros

| Pasta | Origem | Licença |
| --- | --- | --- |
| `vigemclient/` | [ViGEmClient](https://github.com/ViGEm/ViGEmClient) | MIT ([licença](licenses/ViGEmClient-LICENSE.txt)) |
| `../driver/ProconWinBus/` | [ViGEmBus](https://github.com/nefarius/ViGEmBus), pasta `sys/` (commit d986e1d) | BSD-3-Clause ([licença](licenses/ViGEmBus-LICENSE.txt)) |

Mudanças feitas: GUID da interface do barramento e GUID de rastreamento trocados, INF reescrito com o hardware ID `Root\ProconWinBus` e o serviço `ProconWinBus`, nomes e textos do recurso de versão trocados, e o projeto ajustado para o WDK atual. O resto do código está como no original.

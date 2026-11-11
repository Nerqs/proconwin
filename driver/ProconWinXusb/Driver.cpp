// ProconWinXusb: controle de Xbox 360 virtual em modo usuário (UMDF2).
//
// Por que modo usuário: drivers de kernel precisam de assinatura da Microsoft
// (ou do modo de teste do Windows). Drivers UMDF rodam dentro de um processo
// comum do Windows (WUDFHost.exe) e não passam pela verificação de código do
// kernel, então funcionam com o Secure Boot ligado e sem modo de teste. O
// pacote só precisa de uma assinatura que o PC confie (o instalador cria um
// certificado local).
//
// Como o XInput vê o controle: o XInput procura dispositivos com a interface
// GUID_DEVINTERFACE_XUSB e conversa com eles por IOCTLs (o mesmo protocolo do
// xusb22.sys, o driver dos controles de Xbox 360 com fio). Este driver
// responde a esses IOCTLs com o estado que o procon_virtual manda pela
// interface FEED. O protocolo XUSB não é documentado pela Microsoft; os
// formatos abaixo vêm de projetos que reimplementam o XInput.
#include <windows.h>
#include <wdf.h>

#include <cstring>

#include "proconwin_xusb.h"

namespace {

const GUID kXusbInterface = PROCONWIN_XUSB_INTERFACE_GUID_INIT;
const GUID kFeedInterface = PROCONWIN_FEED_INTERFACE_GUID_INIT;

// IOCTLs do XUSB (dispositivo 0x8000, METHOD_BUFFERED).
constexpr ULONG kIoctlXusbGetInformation = 0x80006000;
constexpr ULONG kIoctlXusbGetCapabilities = 0x8000E004;
constexpr ULONG kIoctlXusbGetLedState = 0x8000E008;
constexpr ULONG kIoctlXusbGetState = 0x8000E00C;
constexpr ULONG kIoctlXusbSetState = 0x8000A010;
constexpr ULONG kIoctlXusbWaitGuideButton = 0x8000A014;
constexpr ULONG kIoctlXusbGetBatteryInformation = 0x8000E018;
constexpr ULONG kIoctlXusbPowerDownDevice = 0x8000A01C;

constexpr USHORT kXusbVersion11 = 0x0101;
constexpr USHORT kXusbVersion12 = 0x0102;

// Identidade do controle virtual: Xbox 360 com fio da Microsoft. Os jogos
// reconhecem esse VID/PID e usam o layout de botões certo.
constexpr USHORT kVendorId = 0x045E;
constexpr USHORT kProductId = 0x028E;

constexpr USHORT kGuideBit = 0x0400;
constexpr UCHAR kSetStateLed = 0x01;
constexpr UCHAR kSetStateRumble = 0x02;

#pragma pack(push, 1)
struct XusbRequest {  // entrada da maioria dos IOCTLs
  USHORT version;
  UCHAR index;
};

struct XusbInformation {
  USHORT version;
  UCHAR device_count;
  UCHAR unknown1;
  UCHAR flags;  // 0x80 = "ignore esta interface"
  UCHAR unknown2;
  USHORT unknown3;
  USHORT vendor_id;
  USHORT product_id;
};
static_assert(sizeof(XusbInformation) == 12, "XusbInformation");

struct XusbState {
  USHORT version;
  UCHAR status;  // 1 = conectado
  UCHAR unknown1;
  UCHAR input_id;
  ULONG packet_number;
  UCHAR unknown2;
  UCHAR unknown3;
  USHORT buttons;
  UCHAR left_trigger;
  UCHAR right_trigger;
  SHORT lx, ly, rx, ry;
  UCHAR unknown4[5];
  UCHAR extra_buttons;
};
static_assert(sizeof(XusbState) == 29, "XusbState");

struct XusbSetState {
  UCHAR index;
  UCHAR led;
  UCHAR large_motor;
  UCHAR small_motor;
  UCHAR flags;
};
static_assert(sizeof(XusbSetState) == 5, "XusbSetState");

struct XusbLedState {
  USHORT version;
  UCHAR led;
};

struct XusbCapabilities11 {
  USHORT version;
  UCHAR type;
  UCHAR subtype;
  USHORT buttons;
  UCHAR left_trigger;
  UCHAR right_trigger;
  SHORT lx, ly, rx, ry;
  UCHAR unknown[6];
  UCHAR large_motor;
  UCHAR small_motor;
};
static_assert(sizeof(XusbCapabilities11) == 24, "XusbCapabilities11");

struct XusbCapabilities12 {
  USHORT version;
  UCHAR type;
  UCHAR subtype;
  USHORT flags;
  USHORT vendor_id;
  USHORT product_id;
  USHORT product_version;
  ULONG unknown1;
  USHORT buttons;
  UCHAR left_trigger;
  UCHAR right_trigger;
  SHORT lx, ly, rx, ry;
  ULONG unknown2;
  UCHAR unknown3;
  UCHAR unknown4;
  UCHAR large_motor;
  UCHAR small_motor;
};
static_assert(sizeof(XusbCapabilities12) == 36, "XusbCapabilities12");

struct XusbBatteryRequest {
  USHORT version;
  UCHAR index;
  UCHAR device_type;
};

struct XusbBattery {
  USHORT version;
  UCHAR type;   // 1 = com fio
  UCHAR level;  // 3 = cheia
};
#pragma pack(pop)

struct DeviceContext {
  SRWLOCK lock;
  WDFFILEOBJECT feeder;  // programa que alimenta o controle (ou nullptr)
  ProconwinReport report;
  ULONG packet_number;
  UCHAR led;
  UCHAR large_motor;
  UCHAR small_motor;
  ULONG feedback_seq;
  WDFQUEUE guide_queue;     // pedidos do XInput esperando o botão Guide
  WDFQUEUE feedback_queue;  // pedidos do programa esperando vibração/LED
};
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DeviceContext, GetContext)

class Lock {
 public:
  explicit Lock(DeviceContext* ctx) : ctx_(ctx) { AcquireSRWLockExclusive(&ctx_->lock); }
  ~Lock() { ReleaseSRWLockExclusive(&ctx_->lock); }
  Lock(const Lock&) = delete;
  Lock& operator=(const Lock&) = delete;

 private:
  DeviceContext* ctx_;
};

UCHAR LedToPlayer(UCHAR led) {
  if (led >= 6 && led <= 9) return static_cast<UCHAR>(led - 6);  // aceso fixo
  if (led >= 2 && led <= 5) return static_cast<UCHAR>(led - 2);  // piscando
  return 0xFF;
}

// Precisa ser chamado com o lock.
void FillState(const DeviceContext* ctx, USHORT version, XusbState* out) {
  RtlZeroMemory(out, sizeof(*out));
  out->version = version;
  out->status = ctx->feeder ? 1 : 0;
  out->packet_number = ctx->packet_number;
  out->buttons = ctx->report.buttons;
  out->left_trigger = ctx->report.left_trigger;
  out->right_trigger = ctx->report.right_trigger;
  out->lx = ctx->report.lx;
  out->ly = ctx->report.ly;
  out->rx = ctx->report.rx;
  out->ry = ctx->report.ry;
}

// Precisa ser chamado com o lock.
void FillFeedback(const DeviceContext* ctx, ProconwinFeedback* out) {
  out->seq = ctx->feedback_seq;
  out->large_motor = ctx->large_motor;
  out->small_motor = ctx->small_motor;
  out->led = ctx->led;
  out->player = LedToPlayer(ctx->led);
}

// Tira da fila (com o lock) e completa (sem o lock) os pedidos pendentes.
// Até 16 por vez: na prática há um ou dois.
constexpr int kMaxPending = 16;

struct Completion {
  WDFREQUEST req;
  NTSTATUS status;
  size_t bytes;
};

void CompleteAll(Completion* list, int n) {
  for (int i = 0; i < n; ++i) {
    WdfRequestCompleteWithInformation(list[i].req, list[i].status, list[i].bytes);
  }
}

// Com o lock: responde todos os pedidos de feedback pendentes.
int DrainFeedback(DeviceContext* ctx, Completion* list, int n) {
  WDFREQUEST req;
  while (n < kMaxPending && NT_SUCCESS(WdfIoQueueRetrieveNextRequest(ctx->feedback_queue, &req))) {
    ProconwinFeedback* out = nullptr;
    NTSTATUS st = WdfRequestRetrieveOutputBuffer(req, sizeof(*out), reinterpret_cast<PVOID*>(&out),
                                                 nullptr);
    if (NT_SUCCESS(st)) FillFeedback(ctx, out);
    list[n++] = {req, st, NT_SUCCESS(st) ? sizeof(*out) : 0};
  }
  return n;
}

// Com o lock: responde os pedidos que esperam o botão Guide.
int DrainGuide(DeviceContext* ctx, Completion* list, int n) {
  WDFREQUEST req;
  while (n < kMaxPending && NT_SUCCESS(WdfIoQueueRetrieveNextRequest(ctx->guide_queue, &req))) {
    XusbState* out = nullptr;
    NTSTATUS st = WdfRequestRetrieveOutputBuffer(req, sizeof(*out), reinterpret_cast<PVOID*>(&out),
                                                 nullptr);
    if (NT_SUCCESS(st)) FillState(ctx, kXusbVersion12, out);
    list[n++] = {req, st, NT_SUCCESS(st) ? sizeof(*out) : 0};
  }
  return n;
}

void SetXusbInterface(WDFDEVICE device, bool on) {
  WdfDeviceSetDeviceInterfaceState(device, &kXusbInterface, nullptr, on ? TRUE : FALSE);
}

// ---- IOCTLs do nosso programa ----

void HandleFeed(WDFDEVICE device, DeviceContext* ctx, WDFREQUEST req, ULONG code) {
  WDFFILEOBJECT file = WdfRequestGetFileObject(req);
  Completion done[kMaxPending];
  int n = 0;

  if (code == IOCTL_PROCONWIN_REGISTER) {
    bool became_feeder = false;
    NTSTATUS st = STATUS_SUCCESS;
    {
      Lock l(ctx);
      if (ctx->feeder && ctx->feeder != file) {
        st = STATUS_DEVICE_BUSY;
      } else if (!ctx->feeder) {
        ctx->feeder = file;
        RtlZeroMemory(&ctx->report, sizeof(ctx->report));
        ++ctx->packet_number;
        became_feeder = true;
      }
    }
    // Liga a interface do XInput: o Windows avisa que um controle chegou.
    if (became_feeder) SetXusbInterface(device, true);
    WdfRequestComplete(req, st);
    return;
  }

  if (code == IOCTL_PROCONWIN_SUBMIT) {
    ProconwinReport* in = nullptr;
    NTSTATUS st = WdfRequestRetrieveInputBuffer(req, sizeof(*in), reinterpret_cast<PVOID*>(&in),
                                                nullptr);
    if (NT_SUCCESS(st)) {
      Lock l(ctx);
      if (ctx->feeder != file) {
        st = STATUS_ACCESS_DENIED;
      } else {
        bool guide_pressed = (in->buttons & kGuideBit) && !(ctx->report.buttons & kGuideBit);
        if (std::memcmp(&ctx->report, in, sizeof(*in)) != 0) {
          ctx->report = *in;
          ++ctx->packet_number;
        }
        if (guide_pressed) n = DrainGuide(ctx, done, n);
      }
    }
    CompleteAll(done, n);
    WdfRequestComplete(req, st);
    return;
  }

  if (code == IOCTL_PROCONWIN_WAIT_FEEDBACK) {
    ULONG* seen = nullptr;
    ProconwinFeedback* out = nullptr;
    NTSTATUS st = WdfRequestRetrieveInputBuffer(req, sizeof(*seen), reinterpret_cast<PVOID*>(&seen),
                                                nullptr);
    if (NT_SUCCESS(st)) {
      st = WdfRequestRetrieveOutputBuffer(req, sizeof(*out), reinterpret_cast<PVOID*>(&out), nullptr);
    }
    if (!NT_SUCCESS(st)) {
      WdfRequestComplete(req, st);
      return;
    }
    {
      Lock l(ctx);
      if (ctx->feeder != file) {
        st = STATUS_ACCESS_DENIED;
      } else if (*seen != ctx->feedback_seq) {
        FillFeedback(ctx, out);
      } else {
        // Nada novo: espera. A fila é mexida com o lock para não perder uma
        // mudança que chegue entre a verificação e o enfileiramento.
        st = WdfRequestForwardToIoQueue(req, ctx->feedback_queue);
        if (NT_SUCCESS(st)) return;
      }
    }
    WdfRequestCompleteWithInformation(req, st, NT_SUCCESS(st) ? sizeof(*out) : 0);
    return;
  }

  WdfRequestComplete(req, STATUS_INVALID_DEVICE_REQUEST);
}

// ---- IOCTLs do XInput ----

bool CheckIndex(WDFREQUEST req, size_t min_in, PVOID* in) {
  size_t len = 0;
  if (!NT_SUCCESS(WdfRequestRetrieveInputBuffer(req, min_in, in, &len))) return false;
  // Este barramento tem um controle só: índice 0.
  return static_cast<const XusbRequest*>(*in)->index == 0;
}

void HandleXusb(DeviceContext* ctx, WDFREQUEST req, ULONG code) {
  NTSTATUS st = STATUS_SUCCESS;
  size_t bytes = 0;
  PVOID in = nullptr;
  PVOID out = nullptr;
  size_t out_len = 0;
  Completion done[kMaxPending];
  int n = 0;

  switch (code) {
    case kIoctlXusbGetInformation: {
      st = WdfRequestRetrieveOutputBuffer(req, sizeof(XusbInformation), &out, nullptr);
      if (!NT_SUCCESS(st)) break;
      auto* info = static_cast<XusbInformation*>(out);
      RtlZeroMemory(info, sizeof(*info));
      info->version = kXusbVersion12;
      info->device_count = 1;
      info->vendor_id = kVendorId;
      info->product_id = kProductId;
      bytes = sizeof(*info);
      break;
    }

    case kIoctlXusbGetState: {
      if (!CheckIndex(req, sizeof(XusbRequest), &in)) {
        st = STATUS_INVALID_PARAMETER;
        break;
      }
      st = WdfRequestRetrieveOutputBuffer(req, sizeof(XusbState), &out, nullptr);
      if (!NT_SUCCESS(st)) break;
      Lock l(ctx);
      FillState(ctx, static_cast<const XusbRequest*>(in)->version, static_cast<XusbState*>(out));
      bytes = sizeof(XusbState);
      break;
    }

    case kIoctlXusbSetState: {
      size_t len = 0;
      st = WdfRequestRetrieveInputBuffer(req, sizeof(XusbSetState), &in, &len);
      if (!NT_SUCCESS(st)) break;
      const auto* s = static_cast<const XusbSetState*>(in);
      if (s->index != 0) {
        st = STATUS_INVALID_PARAMETER;
        break;
      }
      Lock l(ctx);
      bool changed = false;
      if ((s->flags & kSetStateLed) && s->led != ctx->led) {
        ctx->led = s->led;
        changed = true;
      }
      if ((s->flags & kSetStateRumble) &&
          (s->large_motor != ctx->large_motor || s->small_motor != ctx->small_motor)) {
        ctx->large_motor = s->large_motor;
        ctx->small_motor = s->small_motor;
        changed = true;
      }
      if (changed) {
        ++ctx->feedback_seq;
        n = DrainFeedback(ctx, done, n);
      }
      break;
    }

    case kIoctlXusbGetLedState: {
      if (!CheckIndex(req, sizeof(XusbRequest), &in)) {
        st = STATUS_INVALID_PARAMETER;
        break;
      }
      st = WdfRequestRetrieveOutputBuffer(req, sizeof(XusbLedState), &out, nullptr);
      if (!NT_SUCCESS(st)) break;
      auto* led = static_cast<XusbLedState*>(out);
      Lock l(ctx);
      led->version = static_cast<const XusbRequest*>(in)->version;
      led->led = ctx->led;
      bytes = sizeof(*led);
      break;
    }

    case kIoctlXusbGetCapabilities: {
      if (!CheckIndex(req, sizeof(XusbRequest), &in)) {
        st = STATUS_INVALID_PARAMETER;
        break;
      }
      USHORT version = static_cast<const XusbRequest*>(in)->version;
      st = WdfRequestRetrieveOutputBuffer(req, sizeof(XusbCapabilities11), &out, &out_len);
      if (!NT_SUCCESS(st)) break;
      if (version >= kXusbVersion12) {
        if (out_len < sizeof(XusbCapabilities12)) {
          st = STATUS_BUFFER_TOO_SMALL;
          break;
        }
        auto* c = static_cast<XusbCapabilities12*>(out);
        RtlZeroMemory(c, sizeof(*c));
        c->version = version;
        c->type = 1;     // XINPUT_DEVTYPE_GAMEPAD
        c->subtype = 1;  // XINPUT_DEVSUBTYPE_GAMEPAD
        c->flags = 0x01; // tem vibração
        c->vendor_id = kVendorId;
        c->product_id = kProductId;
        c->buttons = 0xF7FF;
        c->left_trigger = c->right_trigger = 0xFF;
        c->lx = c->ly = c->rx = c->ry = static_cast<SHORT>(0xFFC0);
        c->large_motor = c->small_motor = 0xFF;
        bytes = sizeof(*c);
      } else {
        auto* c = static_cast<XusbCapabilities11*>(out);
        RtlZeroMemory(c, sizeof(*c));
        c->version = version;
        c->type = 1;
        c->subtype = 1;
        c->buttons = 0xF7FF;
        c->left_trigger = c->right_trigger = 0xFF;
        c->lx = c->ly = c->rx = c->ry = static_cast<SHORT>(0xFFC0);
        c->large_motor = c->small_motor = 0xFF;
        bytes = sizeof(*c);
      }
      break;
    }

    case kIoctlXusbGetBatteryInformation: {
      if (!CheckIndex(req, sizeof(XusbBatteryRequest), &in)) {
        st = STATUS_INVALID_PARAMETER;
        break;
      }
      st = WdfRequestRetrieveOutputBuffer(req, sizeof(XusbBattery), &out, nullptr);
      if (!NT_SUCCESS(st)) break;
      auto* b = static_cast<XusbBattery*>(out);
      b->version = static_cast<const XusbBatteryRequest*>(in)->version;
      b->type = 1;   // BATTERY_TYPE_WIRED
      b->level = 3;  // BATTERY_LEVEL_FULL
      bytes = sizeof(*b);
      break;
    }

    case kIoctlXusbWaitGuideButton: {
      if (!CheckIndex(req, sizeof(XusbRequest), &in)) {
        st = STATUS_INVALID_PARAMETER;
        break;
      }
      st = WdfRequestRetrieveOutputBuffer(req, sizeof(XusbState), &out, nullptr);
      if (!NT_SUCCESS(st)) break;
      st = WdfRequestForwardToIoQueue(req, ctx->guide_queue);
      if (NT_SUCCESS(st)) return;  // completa quando o Guide for apertado
      break;
    }

    case kIoctlXusbPowerDownDevice:
      // "Desligar o controle" não faz sentido aqui; só confirma.
      break;

    default:
      // Áudio do headset, informações do barramento USB, etc.
      st = STATUS_NOT_SUPPORTED;
      break;
  }

  CompleteAll(done, n);
  WdfRequestCompleteWithInformation(req, st, bytes);
}

// ---- Callbacks do WDF ----

VOID EvtIoDeviceControl(WDFQUEUE queue, WDFREQUEST req, size_t, size_t, ULONG code) {
  WDFDEVICE device = WdfIoQueueGetDevice(queue);
  DeviceContext* ctx = GetContext(device);
  if (code == IOCTL_PROCONWIN_REGISTER || code == IOCTL_PROCONWIN_SUBMIT ||
      code == IOCTL_PROCONWIN_WAIT_FEEDBACK) {
    HandleFeed(device, ctx, req, code);
  } else {
    HandleXusb(ctx, req, code);
  }
}

// Um handle foi fechado. Se era o programa que alimentava o controle, o
// controle virtual some do XInput.
VOID EvtFileCleanup(WDFFILEOBJECT file) {
  WDFDEVICE device = WdfFileObjectGetDevice(file);
  DeviceContext* ctx = GetContext(device);
  Completion done[kMaxPending];
  int n = 0;
  bool was_feeder = false;
  {
    Lock l(ctx);
    WDFREQUEST req;
    while (n < kMaxPending &&
           NT_SUCCESS(WdfIoQueueRetrieveRequestByFileObject(ctx->feedback_queue, file, &req))) {
      done[n++] = {req, STATUS_CANCELLED, 0};
    }
    while (n < kMaxPending &&
           NT_SUCCESS(WdfIoQueueRetrieveRequestByFileObject(ctx->guide_queue, file, &req))) {
      done[n++] = {req, STATUS_CANCELLED, 0};
    }
    if (ctx->feeder == file) {
      was_feeder = true;
      ctx->feeder = nullptr;
      RtlZeroMemory(&ctx->report, sizeof(ctx->report));
      ++ctx->packet_number;
      ctx->large_motor = ctx->small_motor = 0;
      ctx->led = 0;
      ++ctx->feedback_seq;
      // Quem esperava o Guide recebe "desconectado" (status 0).
      n = DrainGuide(ctx, done, n);
    }
  }
  CompleteAll(done, n);
  if (was_feeder) SetXusbInterface(device, false);
}

// Depois que o dispositivo inicia, o WDF liga todas as interfaces. A do
// XInput só deve ficar ligada com o programa conectado.
NTSTATUS EvtSelfManagedIoInit(WDFDEVICE device) {
  DeviceContext* ctx = GetContext(device);
  bool has_feeder;
  {
    Lock l(ctx);
    has_feeder = ctx->feeder != nullptr;
  }
  if (!has_feeder) SetXusbInterface(device, false);
  return STATUS_SUCCESS;
}

NTSTATUS CreateManualQueue(WDFDEVICE device, WDFQUEUE* queue) {
  WDF_IO_QUEUE_CONFIG cfg;
  WDF_IO_QUEUE_CONFIG_INIT(&cfg, WdfIoQueueDispatchManual);
  return WdfIoQueueCreate(device, &cfg, WDF_NO_OBJECT_ATTRIBUTES, queue);
}

NTSTATUS EvtDeviceAdd(WDFDRIVER, PWDFDEVICE_INIT init) {
  WDF_FILEOBJECT_CONFIG file_cfg;
  WDF_FILEOBJECT_CONFIG_INIT(&file_cfg, WDF_NO_EVENT_CALLBACK, WDF_NO_EVENT_CALLBACK,
                             EvtFileCleanup);
  WdfDeviceInitSetFileObjectConfig(init, &file_cfg, WDF_NO_OBJECT_ATTRIBUTES);

  WDF_PNPPOWER_EVENT_CALLBACKS pnp;
  WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnp);
  pnp.EvtDeviceSelfManagedIoInit = EvtSelfManagedIoInit;
  WdfDeviceInitSetPnpPowerEventCallbacks(init, &pnp);

  WDF_OBJECT_ATTRIBUTES attrs;
  WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attrs, DeviceContext);
  WDFDEVICE device;
  NTSTATUS st = WdfDeviceCreate(&init, &attrs, &device);
  if (!NT_SUCCESS(st)) return st;

  DeviceContext* ctx = GetContext(device);
  InitializeSRWLock(&ctx->lock);

  WDF_IO_QUEUE_CONFIG qcfg;
  WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&qcfg, WdfIoQueueDispatchParallel);
  qcfg.EvtIoDeviceControl = EvtIoDeviceControl;
  st = WdfIoQueueCreate(device, &qcfg, WDF_NO_OBJECT_ATTRIBUTES, nullptr);
  if (!NT_SUCCESS(st)) return st;
  st = CreateManualQueue(device, &ctx->guide_queue);
  if (!NT_SUCCESS(st)) return st;
  st = CreateManualQueue(device, &ctx->feedback_queue);
  if (!NT_SUCCESS(st)) return st;

  st = WdfDeviceCreateDeviceInterface(device, &kFeedInterface, nullptr);
  if (!NT_SUCCESS(st)) return st;
  return WdfDeviceCreateDeviceInterface(device, &kXusbInterface, nullptr);
}

}  // namespace

extern "C" NTSTATUS DriverEntry(PDRIVER_OBJECT driver_object, PUNICODE_STRING registry_path) {
  WDF_DRIVER_CONFIG cfg;
  WDF_DRIVER_CONFIG_INIT(&cfg, EvtDeviceAdd);
  return WdfDriverCreate(driver_object, registry_path, WDF_NO_OBJECT_ATTRIBUTES, &cfg,
                         WDF_NO_HANDLE);
}

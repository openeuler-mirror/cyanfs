#include <Uefi.h>
#include <Protocol/DriverBinding.h>
#include <Protocol/ComponentName2.h>
#include <Protocol/ComponentName.h>
#include <Protocol/BlockIo.h>
#include <Protocol/Cyanfs.h>
#include <Library/UefiLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiDriverEntryPoint.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/DevicePathLib.h>
#include <Library/DebugLib.h>

#include "../../core/core.h"

typedef struct {
	struct cyanfs_super *Super;
	EFI_BLOCK_IO_PROTOCOL *Disk;
	EFI_CYANFS_PROTOCOL Protocol;
} CYANFS_BACKEND;

// 只支持512字节的逻辑块
#define SECTOR_SHIFT (9)
#define SECTOR_SIZE (1 << SECTOR_SHIFT)

static inline CYANFS_BACKEND *ToBackend(EFI_CYANFS_PROTOCOL *Cyanfs)
{
	return cyanfs_container_of(Cyanfs, CYANFS_BACKEND, Protocol);
}

void CyanfsBackendLoop(IN CYANFS_BACKEND *Backend)
{
	struct cyanfs_task *t;
	EFI_BLOCK_IO_PROTOCOL *Disk = Backend->Disk;

	while ((t = cyanfs_super_get_task(Backend->Super)) != NULL) {
		EFI_STATUS Status = EFI_SUCCESS;
		switch (t->type) {
		case CYANFS_TASK_NOP:
			break;
		case CYANFS_TASK_READ:
			Status = Disk->ReadBlocks(Disk, Disk->Media->MediaId, t->read.b_off >> SECTOR_SHIFT,
						  t->read.len, t->read.buf);
			break;
		case CYANFS_TASK_WRITE:
			Status = Disk->WriteBlocks(Disk, Disk->Media->MediaId, t->write.b_off >> SECTOR_SHIFT,
						   t->write.len, t->write.buf);
			break;
		case CYANFS_TASK_FLUSH:
			Status = Disk->FlushBlocks(Disk);
			break;
		default:
			CYANFS_BUG_ON(1);
		}
		t->done(t, Status);
	}
}

void CyanfsBackendNewTask(IN VOID *Ctx)
{
	CyanfsBackendLoop(Ctx);
}

EFI_STATUS EFIAPI CyanfsStatfs(IN EFI_CYANFS_PROTOCOL *Cyanfs, OUT CYANFS_SUPER_META *Meta)
{
	return cyanfs_statfs(ToBackend(Cyanfs)->Super, (struct cyanfs_super_meta *)Meta);
}

EFI_STATUS EFIAPI CyanfsList(IN EFI_CYANFS_PROTOCOL *Cyanfs, IN OUT CYANFS_FILE_META *Meta)
{
	return cyanfs_list(ToBackend(Cyanfs)->Super, (struct cyanfs_file_meta *)Meta);
}

EFI_STATUS EFIAPI CyanfsLookup(IN EFI_CYANFS_PROTOCOL *Cyanfs, IN CYANFS_FILE_NAME Name, OUT CYANFS_FILE_META *Meta)
{
	return cyanfs_lookup(ToBackend(Cyanfs)->Super, *(cyanfs_file_name_t *)(&Name), (struct cyanfs_file_meta *)Meta);
}

EFI_STATUS EFIAPI CyanfsCreate(IN EFI_CYANFS_PROTOCOL *Cyanfs, IN CYANFS_FILE_NAME Name, OUT CYANFS_FILE_ID *ID)
{
	CYANFS_BACKEND *Backend = ToBackend(Cyanfs);
	EFI_STATUS Status;
	Status = cyanfs_create(Backend->Super, *(cyanfs_file_name_t *)(&Name), ID);
	cyanfs_super_flush(Backend->Super, 1);
	return Status;
}

EFI_STATUS EFIAPI CyanfsFork(IN EFI_CYANFS_PROTOCOL *Cyanfs, IN CYANFS_FILE_ID From, IN CYANFS_FILE_NAME Name,
			     OUT CYANFS_FILE_ID *ID)
{
	CYANFS_BACKEND *Backend = ToBackend(Cyanfs);
	EFI_STATUS Status;
	Status = cyanfs_fork(Backend->Super, From, *(cyanfs_file_name_t *)(&Name), ID);
	cyanfs_super_flush(Backend->Super, 1);
	return Status;
}

EFI_STATUS EFIAPI CyanfsRename(IN EFI_CYANFS_PROTOCOL *Cyanfs, IN CYANFS_FILE_ID From, IN CYANFS_FILE_NAME Name)
{
	CYANFS_BACKEND *Backend = ToBackend(Cyanfs);
	EFI_STATUS Status;
	Status = cyanfs_rename(Backend->Super, From, *(cyanfs_file_name_t *)(&Name));
	cyanfs_super_flush(Backend->Super, 1);
	return Status;
}

EFI_STATUS EFIAPI CyanfsTruncate(IN EFI_CYANFS_PROTOCOL *Cyanfs, IN CYANFS_FILE_ID ID, IN UINT64 Size)
{
	CYANFS_BACKEND *Backend = ToBackend(Cyanfs);
	EFI_STATUS Status;
	Status = cyanfs_truncate(Backend->Super, ID, Size);
	cyanfs_super_flush(Backend->Super, 1);
	return Status;
}

EFI_STATUS EFIAPI CyanfsDelete(IN EFI_CYANFS_PROTOCOL *Cyanfs, IN CYANFS_FILE_ID ID)
{
	CYANFS_BACKEND *Backend = ToBackend(Cyanfs);
	EFI_STATUS Status;
	Status = cyanfs_delete(Backend->Super, ID);
	cyanfs_super_flush(Backend->Super, 1);
	return Status;
}

typedef struct {
	EFI_HANDLE Handle;

	struct cyanfs_file *File;
	CYANFS_BACKEND *Backend;

	EFI_BLOCK_IO_PROTOCOL BlockIo;
	EFI_BLOCK_IO_MEDIA Media;
	MEDIA_CYANFS_DISK_DEVICE_PATH DeviceNode;
	EFI_DEVICE_PATH_PROTOCOL *DevicePath;
} CYANFS_DISK;

typedef struct {
	CYANFS_DISK *Disk;
	VOID *Buffer;
	UINT64 Offset;
	BOOLEAN IsWrite;
} CYANFS_IO_CTX;

EFI_STATUS CyanfsIo(CYANFS_DISK *Disk, BOOLEAN IsWrite, UINT64 Offset, UINT64 Len, VOID *Buffer);

EFI_STATUS CyanfsIoPartial(cyanfs_map_type_t Type, uint64_t Offset, uint64_t Len, void *C)
{
	CYANFS_IO_CTX *Ctx = C;
	CYANFS_BACKEND *Backend = Ctx->Disk->Backend;
	EFI_BLOCK_IO_PROTOCOL *Disk = Backend->Disk;
	EFI_STATUS Status = EFI_NO_MEDIA;

	switch (Type) {
	case CYANFS_MAP_NOP:
		if (!Ctx->IsWrite)
			ZeroMem(Ctx->Buffer, Len);
		Status = EFI_SUCCESS;
		break;
	case CYANFS_MAP_SUBMIT:
		if (Ctx->IsWrite)
			Status =
				Disk->WriteBlocks(Disk, Disk->Media->MediaId, Offset >> SECTOR_SHIFT, Len, Ctx->Buffer);
		else
			Status = Disk->ReadBlocks(Disk, Disk->Media->MediaId, Offset >> SECTOR_SHIFT, Len, Ctx->Buffer);
		break;
	case CYANFS_MAP_REQUEUE:
		CyanfsBackendLoop(Backend);
		Status = CyanfsIo(Ctx->Disk, Ctx->IsWrite, Ctx->Offset, Len, Ctx->Buffer);
		break;
	default:
		CYANFS_BUG_ON(1);
	}

	if (!EFI_ERROR(Status)) {
		Ctx->Buffer = (UINT8 *)Ctx->Buffer + Len;
		Ctx->Offset += Len;
	}

	return Status;
}

EFI_STATUS CyanfsIo(CYANFS_DISK *Disk, BOOLEAN IsWrite, UINT64 Offset, UINT64 Len, VOID *Buffer)
{
	CYANFS_IO_CTX Ctx = {
		.Disk = Disk,
		.Buffer = Buffer,
		.IsWrite = IsWrite,
		.Offset = Offset,
	};

	if (!Len) {
		return EFI_SUCCESS;
	}

	if (IsWrite)
		return cyanfs_write(Disk->File, &Ctx, Offset, Len, CyanfsIoPartial);
	else
		return cyanfs_read(Disk->File, &Ctx, Offset, Len, CyanfsIoPartial);
}

EFI_STATUS EFIAPI CyanfsIoRead(IN EFI_BLOCK_IO_PROTOCOL *This, IN UINT32 MediaId, IN EFI_LBA Lba, IN UINTN BufferSize,
			       OUT VOID *Buffer)
{
	CYANFS_DISK *Disk = cyanfs_container_of(This, CYANFS_DISK, BlockIo);
	return CyanfsIo(Disk, FALSE, Lba << SECTOR_SHIFT, BufferSize, Buffer);
}

EFI_STATUS EFIAPI CyanfsIoWrite(IN EFI_BLOCK_IO_PROTOCOL *This, IN UINT32 MediaId, IN EFI_LBA Lba, IN UINTN BufferSize,
				IN VOID *Buffer)
{
	CYANFS_DISK *Disk = cyanfs_container_of(This, CYANFS_DISK, BlockIo);
	return CyanfsIo(Disk, TRUE, Lba << SECTOR_SHIFT, BufferSize, Buffer);
}

EFI_STATUS EFIAPI CyanfsIoFlush(IN EFI_BLOCK_IO_PROTOCOL *This)
{
	CYANFS_DISK *Disk = cyanfs_container_of(This, CYANFS_DISK, BlockIo);
	EFI_STATUS Status;
	cyanfs_map_type_t map;

	Status = cyanfs_flush(Disk->File, &map);
	if (EFI_ERROR(Status))
		return Status;

	switch (map) {
	case CYANFS_MAP_NOP:
		break;
	case CYANFS_MAP_REQUEUE:
		CyanfsBackendLoop(Disk->Backend);
	case CYANFS_MAP_SUBMIT:
		Status = Disk->Backend->Disk->FlushBlocks(Disk->Backend->Disk);
		break;
	default:
		CYANFS_BUG_ON(1);
	}

	return Status;
}

EFI_STATUS EFIAPI CyanfsIoReset(IN EFI_BLOCK_IO_PROTOCOL *This, IN BOOLEAN ExtendedVerification)
{
	return EFI_SUCCESS;
}

EFI_STATUS EFIAPI CyanfsMap(IN EFI_CYANFS_PROTOCOL *Cyanfs, IN CYANFS_FILE_ID ID,
			    IN EFI_DEVICE_PATH *ParentDevicePath OPTIONAL, OUT EFI_DEVICE_PATH_PROTOCOL **DevicePath)
{
	CYANFS_BACKEND *Backend = ToBackend(Cyanfs);
	EFI_STATUS Status;
	CYANFS_DISK *Disk;
	struct cyanfs_file *File;
	struct cyanfs_super_meta Meta;

	Status = cyanfs_statfs(Backend->Super, &Meta);
	if (EFI_ERROR(Status)) {
		goto Exit;
	}

	Status = cyanfs_open(Backend->Super, ID, !Backend->Disk->Media->ReadOnly, &File);
	if (EFI_ERROR(Status)) {
		goto Exit;
	}

	if (!cyanfs_size(File)) {
		Status = EFI_INVALID_PARAMETER;
		goto CloseFile;
	}

	Disk = AllocateZeroPool(sizeof(CYANFS_DISK));
	if (!Disk) {
		Status = EFI_OUT_OF_RESOURCES;
		goto CloseFile;
	}

	Disk->Backend = Backend;
	Disk->File = File;

	Disk->Media.RemovableMedia = FALSE;
	Disk->Media.MediaPresent = TRUE;
	Disk->Media.LogicalPartition = FALSE;
	Disk->Media.ReadOnly = Backend->Disk->Media->ReadOnly;
	Disk->Media.WriteCaching = TRUE;
	Disk->Media.BlockSize = SECTOR_SIZE;
	Disk->Media.LastBlock = (cyanfs_size(File) - 1) >> SECTOR_SHIFT;

	Disk->BlockIo.Media = &Disk->Media;
	Disk->BlockIo.Revision = EFI_BLOCK_IO_PROTOCOL_REVISION;
	Disk->BlockIo.ReadBlocks = CyanfsIoRead;
	Disk->BlockIo.WriteBlocks = CyanfsIoWrite;
	Disk->BlockIo.FlushBlocks = CyanfsIoFlush;
	Disk->BlockIo.Reset = CyanfsIoReset;

	Disk->DeviceNode.Header.Type = MEDIA_DEVICE_PATH;
	Disk->DeviceNode.Header.SubType = 0x59;
	Disk->DeviceNode.Header.Length[0] = (UINT8)sizeof(MEDIA_CYANFS_DISK_DEVICE_PATH);
	Disk->DeviceNode.Header.Length[1] = (UINT8)(sizeof(MEDIA_CYANFS_DISK_DEVICE_PATH) >> 8);
	CopyMem(&Disk->DeviceNode.UUID, &Meta.uuid, sizeof(Meta.uuid));
	Disk->DeviceNode.FileID = ID;

	*DevicePath = AppendDevicePathNode(ParentDevicePath, (EFI_DEVICE_PATH_PROTOCOL *)&Disk->DeviceNode);
	if (!*DevicePath) {
		Status = EFI_OUT_OF_RESOURCES;
		goto FreeDisk;
	}
	Disk->DevicePath = *DevicePath;

	Status = gBS->InstallMultipleProtocolInterfaces(&Disk->Handle, &gEfiBlockIoProtocolGuid, &Disk->BlockIo,
							&gEfiDevicePathProtocolGuid, Disk->DevicePath, NULL);
	if (EFI_ERROR(Status)) {
		goto FreeDisk;
	}

	CYANFS_DEBUG("Register VDisk %p Handle %p Size: %llu", Disk, Disk->Handle, cyanfs_size(File));
	gBS->ConnectController(Disk->Handle, NULL, NULL, TRUE);

	return EFI_SUCCESS;

FreeDisk:
	FreePool(Disk);
CloseFile:
	cyanfs_close(File);
Exit:
	return Status;
}

EFI_STATUS EFIAPI CyanfsDriverStart(IN EFI_DRIVER_BINDING_PROTOCOL *This, IN EFI_HANDLE Controller,
				    IN EFI_DEVICE_PATH_PROTOCOL *RemainingDevicePath)
{
	EFI_STATUS Status;
	EFI_BLOCK_IO_PROTOCOL *Disk;
	EFI_CYANFS_PROTOCOL *Cyanfs;
	CYANFS_BACKEND *Backend;

	Status = gBS->OpenProtocol(Controller, &gEfiCyanfsProtocolGuid, NULL, This->DriverBindingHandle, Controller,
				   EFI_OPEN_PROTOCOL_TEST_PROTOCOL);
	if (!EFI_ERROR(Status)) {
		Status = EFI_ALREADY_STARTED;
		goto Exit;
	}

	Status = gBS->OpenProtocol(Controller, &gEfiBlockIoProtocolGuid, (VOID **)&Disk, This->DriverBindingHandle,
				   Controller, EFI_OPEN_PROTOCOL_GET_PROTOCOL);
	if (EFI_ERROR(Status)) {
		Status = EFI_UNSUPPORTED;
		goto Exit;
	}

	Backend = AllocatePool(sizeof(CYANFS_BACKEND));
	if (!Backend) {
		Status = EFI_OUT_OF_RESOURCES;
		goto Exit;
	}

	Backend->Disk = Disk;
	Backend->Super = cyanfs_super_open(Disk->Media->LastBlock << SECTOR_SHIFT, 0, 0);
	if (!Backend->Super) {
		Status = EFI_OUT_OF_RESOURCES;
		goto FreeProtocol;
	}

	Cyanfs = &Backend->Protocol;
	Cyanfs->Statfs = CyanfsStatfs;
	Cyanfs->List = CyanfsList;
	Cyanfs->Lookup = CyanfsLookup;
	Cyanfs->Create = CyanfsCreate;
	Cyanfs->Fork = CyanfsFork;
	Cyanfs->Rename = CyanfsRename;
	Cyanfs->Truncate = CyanfsTruncate;
	Cyanfs->Delete = CyanfsDelete;
	Cyanfs->Map = CyanfsMap;

	CyanfsBackendLoop(Backend);

	if (!cyanfs_super_is_ready(Backend->Super)) {
		Status = EFI_VOLUME_CORRUPTED;
		goto FreeSuper;
	}
	cyanfs_super_set_new_task_callback(Backend->Super, Backend, CyanfsBackendNewTask);

	Status = gBS->InstallMultipleProtocolInterfaces(&Controller, &gEfiCyanfsProtocolGuid, Cyanfs, NULL);
	if (EFI_ERROR(Status)) {
		goto FreeSuper;
	}

	CYANFS_DEBUG("BackendDiskHandle %p, BackendDiskProtocol %p, Cyanfs %p", Controller, Disk, Cyanfs);
	return EFI_SUCCESS;

FreeSuper:
	cyanfs_super_close(Backend->Super);
FreeProtocol:
	FreePool(Backend);
Exit:
	return Status;
}

EFI_STATUS EFIAPI CyanfsDriverStop(IN EFI_DRIVER_BINDING_PROTOCOL *This, IN EFI_HANDLE Controller,
				   IN UINTN NumberOfChildren, IN EFI_HANDLE *ChildHandleBuffer)
{
	EFI_STATUS Status;
	EFI_CYANFS_PROTOCOL *Cyanfs;
	CYANFS_BACKEND *Backend;

	CYANFS_BUG_ON(NumberOfChildren != 0);

	CYANFS_DEBUG("BackendDiskHandle", Controller);
	Status = gBS->OpenProtocol(Controller, &gEfiCyanfsProtocolGuid, (VOID **)&Cyanfs, This->DriverBindingHandle,
				   Controller, EFI_OPEN_PROTOCOL_GET_PROTOCOL);
	if (EFI_ERROR(Status)) {
		return EFI_INVALID_PARAMETER;
	}

	Backend = ToBackend(Cyanfs);
	cyanfs_super_flush(Backend->Super, 1);
	cyanfs_super_close(Backend->Super);

	gBS->CloseProtocol(Controller, &gEfiBlockIoProtocolGuid, This->DriverBindingHandle, Controller);
	gBS->CloseProtocol(Controller, &gEfiCyanfsProtocolGuid, This->DriverBindingHandle, Controller);
	gBS->UninstallProtocolInterface(Controller, &gEfiCyanfsProtocolGuid, Cyanfs);

	FreePool(Backend);
	return EFI_SUCCESS;
}

EFI_STATUS EFIAPI CyanfsDriverSupported(IN EFI_DRIVER_BINDING_PROTOCOL *This, IN EFI_HANDLE Controller,
					IN EFI_DEVICE_PATH_PROTOCOL *RemainingDevicePath)
{
	EFI_STATUS Status;
	EFI_BLOCK_IO_PROTOCOL *Disk;
	UINT8 *Buffer;
	UINT64 DiskSize;
	struct cyanfs_super_header Header;

	Status = gBS->OpenProtocol(Controller, &gEfiCyanfsProtocolGuid, NULL, This->DriverBindingHandle, Controller,
				   EFI_OPEN_PROTOCOL_TEST_PROTOCOL);
	if (!EFI_ERROR(Status)) {
		Status = EFI_ALREADY_STARTED;
		goto Exit;
	}

	Status = gBS->OpenProtocol(Controller, &gEfiBlockIoProtocolGuid, (VOID **)&Disk, This->DriverBindingHandle,
				   Controller, EFI_OPEN_PROTOCOL_GET_PROTOCOL);
	if (EFI_ERROR(Status)) {
		Status = EFI_UNSUPPORTED;
		goto Exit;
	}

	if (Disk->Media->BlockSize != SECTOR_SIZE) {
		Status = EFI_UNSUPPORTED;
		goto Close;
	}

	DiskSize = Disk->Media->LastBlock << SECTOR_SHIFT;
	if (DiskSize < CYANFS_SUPER_BLOCK_SIZE) {
		Status = EFI_UNSUPPORTED;
		goto Close;
	}

	Buffer = AllocatePool(CYANFS_SUPER_BLOCK_SIZE);
	if (!Buffer) {
		Status = EFI_OUT_OF_RESOURCES;
		goto Close;
	}

	Status = Disk->ReadBlocks(Disk, Disk->Media->MediaId, 0, CYANFS_SUPER_BLOCK_SIZE, Buffer);
	if (EFI_ERROR(Status)) {
		goto Free;
	}

	if (cyanfs_super_parse(&Header, Buffer)) {
		Status = EFI_UNSUPPORTED;
		goto Free;
	}

	Status = EFI_SUCCESS;

Free:
	FreePool(Buffer);
Close:
	gBS->CloseProtocol(Controller, &gEfiBlockIoProtocolGuid, This->DriverBindingHandle, Controller);
Exit:
	return Status;
}

EFI_DRIVER_BINDING_PROTOCOL gCyanfsDriverBinding = {
	CyanfsDriverSupported, //
	CyanfsDriverStart, //
	CyanfsDriverStop, //
	0x10,
};

EFI_STATUS EFIAPI CyanfsDriverEntryPoint(IN EFI_HANDLE ImageHandle, IN EFI_SYSTEM_TABLE *SystemTable)
{
	EFI_STATUS Status;
#ifdef CYANFS_DEBUG_ENABLED
	debug_dump_journal = 0;
#endif
	Status = EfiLibInstallDriverBindingComponentName2(ImageHandle, SystemTable, &gCyanfsDriverBinding, ImageHandle,
							  NULL, NULL);
	return Status;
}

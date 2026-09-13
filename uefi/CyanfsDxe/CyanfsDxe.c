/**
 * Copyright (c) 2025 ~ 2026 KylinSec Co., Ltd.
 * cyanfs is licensed under Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *          http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 *
 * Author: huhuikai <huhuikai@kylinsec.com.cn>
 * Author: wenyunchuan <wenyunchuan@kylinsec.com.cn>
 * Author: yuanzhu <yuanzhu@kylinsec.com.cn>
*/
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

	EFI_HANDLE Controller;
	EFI_HANDLE DriverBindingHandle;
	struct cyanfs_list_head Children;
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

static EFI_STATUS CyanfsFlushMetadata(CYANFS_BACKEND *Backend, EFI_STATUS OperationStatus)
{
	EFI_STATUS FlushStatus, CompletionStatus;

	FlushStatus = cyanfs_super_flush(Backend->Super, 1);
	CompletionStatus = cyanfs_super_status(Backend->Super);

	if (EFI_ERROR(OperationStatus))
		return OperationStatus;
	if (EFI_ERROR(FlushStatus))
		return FlushStatus;
	if (EFI_ERROR(CompletionStatus))
		return CompletionStatus;
	return OperationStatus;
}

EFI_STATUS EFIAPI CyanfsCreate(IN EFI_CYANFS_PROTOCOL *Cyanfs, IN CYANFS_FILE_NAME Name, OUT CYANFS_FILE_ID *ID)
{
	CYANFS_BACKEND *Backend = ToBackend(Cyanfs);
	EFI_STATUS Status;
	Status = cyanfs_create(Backend->Super, *(cyanfs_file_name_t *)(&Name), ID);
	return CyanfsFlushMetadata(Backend, Status);
}

EFI_STATUS EFIAPI CyanfsFork(IN EFI_CYANFS_PROTOCOL *Cyanfs, IN CYANFS_FILE_ID From, IN CYANFS_FILE_NAME Name,
			     OUT CYANFS_FILE_ID *ID)
{
	CYANFS_BACKEND *Backend = ToBackend(Cyanfs);
	EFI_STATUS Status;
	Status = cyanfs_fork(Backend->Super, From, *(cyanfs_file_name_t *)(&Name), ID);
	return CyanfsFlushMetadata(Backend, Status);
}

EFI_STATUS EFIAPI CyanfsRename(IN EFI_CYANFS_PROTOCOL *Cyanfs, IN CYANFS_FILE_ID From, IN CYANFS_FILE_NAME Name)
{
	CYANFS_BACKEND *Backend = ToBackend(Cyanfs);
	EFI_STATUS Status;
	Status = cyanfs_rename(Backend->Super, From, *(cyanfs_file_name_t *)(&Name));
	return CyanfsFlushMetadata(Backend, Status);
}

EFI_STATUS EFIAPI CyanfsTruncate(IN EFI_CYANFS_PROTOCOL *Cyanfs, IN CYANFS_FILE_ID ID, IN UINT64 Size)
{
	CYANFS_BACKEND *Backend = ToBackend(Cyanfs);
	EFI_STATUS Status;
	Status = cyanfs_truncate(Backend->Super, ID, Size);
	return CyanfsFlushMetadata(Backend, Status);
}

EFI_STATUS EFIAPI CyanfsDelete(IN EFI_CYANFS_PROTOCOL *Cyanfs, IN CYANFS_FILE_ID ID)
{
	CYANFS_BACKEND *Backend = ToBackend(Cyanfs);
	EFI_STATUS Status;
	Status = cyanfs_delete(Backend->Super, ID);
	return CyanfsFlushMetadata(Backend, Status);
}

typedef enum {
	CYANFS_DISK_CREATING,
	CYANFS_DISK_MANAGED,
	CYANFS_DISK_STOPPING,
	CYANFS_DISK_DEGRADED,
} CYANFS_DISK_STATE;

typedef struct {
	EFI_HANDLE Handle;

	struct cyanfs_file *File;
	CYANFS_BACKEND *Backend;
	struct cyanfs_list_head Link;
	CYANFS_DISK_STATE State;
	BOOLEAN BlockIoInstalled;
	BOOLEAN DevicePathInstalled;
	BOOLEAN ByChildOpened;

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
	EFI_STATUS Status, CompletionStatus;
	cyanfs_map_type_t map;

	Status = cyanfs_flush(Disk->File, &map);
	if (EFI_ERROR(Status))
		return Status;

	switch (map) {
	case CYANFS_MAP_NOP:
		CompletionStatus = cyanfs_file_status(Disk->File);
		if (EFI_ERROR(CompletionStatus))
			return CompletionStatus;
		return Status;
	case CYANFS_MAP_REQUEUE:
		CyanfsBackendLoop(Disk->Backend);
		break;
	case CYANFS_MAP_SUBMIT:
		break;
	default:
		CYANFS_BUG_ON(1);
	}

	Status = Disk->Backend->Disk->FlushBlocks(Disk->Backend->Disk);
	if (EFI_ERROR(Status))
		return Status;
	CompletionStatus = cyanfs_file_status(Disk->File);
	if (EFI_ERROR(CompletionStatus))
		return CompletionStatus;
	return Status;
}

EFI_STATUS EFIAPI CyanfsIoReset(IN EFI_BLOCK_IO_PROTOCOL *This, IN BOOLEAN ExtendedVerification)
{
	return EFI_SUCCESS;
}

static CYANFS_DISK *CyanfsFindChild(CYANFS_BACKEND *Backend, EFI_HANDLE ChildHandle)
{
	struct cyanfs_list_head *Link;

	cyanfs_list_for_each(Link, &Backend->Children) {
		CYANFS_DISK *Disk = cyanfs_container_of(Link, CYANFS_DISK, Link);

		if (Disk->Handle == ChildHandle)
			return Disk;
	}
	return NULL;
}

static VOID CyanfsRefreshDiskProtocols(CYANFS_DISK *Disk)
{
	VOID *Interface = NULL;

	Disk->BlockIoInstalled = FALSE;
	Disk->DevicePathInstalled = FALSE;
	if (!Disk->Handle)
		return;

	if (!EFI_ERROR(gBS->HandleProtocol(Disk->Handle, &gEfiBlockIoProtocolGuid, &Interface)) &&
	    Interface == &Disk->BlockIo)
		Disk->BlockIoInstalled = TRUE;

	Interface = NULL;
	if (!EFI_ERROR(gBS->HandleProtocol(Disk->Handle, &gEfiDevicePathProtocolGuid, &Interface)) &&
	    Interface == Disk->DevicePath)
		Disk->DevicePathInstalled = TRUE;
}

static EFI_STATUS CyanfsOpenChildBlockIo(CYANFS_DISK *Disk)
{
	CYANFS_BACKEND *Backend = Disk->Backend;
	EFI_BLOCK_IO_PROTOCOL *ParentDisk;
	EFI_STATUS Status;

	if (Disk->ByChildOpened)
		return EFI_SUCCESS;

	Status = gBS->OpenProtocol(Backend->Controller, &gEfiBlockIoProtocolGuid, (VOID **)&ParentDisk,
				   Backend->DriverBindingHandle, Disk->Handle,
				   EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER);
	if (EFI_ERROR(Status))
		return Status;
	if (ParentDisk != Backend->Disk) {
		Status = gBS->CloseProtocol(Backend->Controller, &gEfiBlockIoProtocolGuid,
					    Backend->DriverBindingHandle, Disk->Handle);
		if (EFI_ERROR(Status))
			ASSERT_EFI_ERROR(Status);
		return EFI_DEVICE_ERROR;
	}

	Disk->ByChildOpened = TRUE;
	return EFI_SUCCESS;
}

static EFI_STATUS CyanfsCloseChildBlockIo(CYANFS_DISK *Disk)
{
	CYANFS_BACKEND *Backend = Disk->Backend;
	EFI_STATUS Status;

	if (!Disk->ByChildOpened)
		return EFI_SUCCESS;

	Status = gBS->CloseProtocol(Backend->Controller, &gEfiBlockIoProtocolGuid, Backend->DriverBindingHandle,
				    Disk->Handle);
	if (!EFI_ERROR(Status))
		Disk->ByChildOpened = FALSE;
	return Status;
}

static EFI_STATUS CyanfsInstallDiskProtocol(CYANFS_DISK *Disk, EFI_GUID *Protocol, VOID *Interface)
{
	EFI_STATUS Status;

	Status = gBS->InstallMultipleProtocolInterfaces(&Disk->Handle, Protocol, Interface, NULL);
	CyanfsRefreshDiskProtocols(Disk);
	return Status;
}

static EFI_STATUS CyanfsRecoverDisk(CYANFS_DISK *Disk, BOOLEAN Reconnect)
{
	EFI_STATUS Status, FirstError = EFI_SUCCESS;

	CyanfsRefreshDiskProtocols(Disk);
	if (!Disk->BlockIoInstalled && !Disk->DevicePathInstalled) {
		Disk->State = CYANFS_DISK_DEGRADED;
		return EFI_NOT_FOUND;
	}

	Status = CyanfsOpenChildBlockIo(Disk);
	if (EFI_ERROR(Status))
		FirstError = Status;

	if (!Disk->BlockIoInstalled) {
		Status = CyanfsInstallDiskProtocol(Disk, &gEfiBlockIoProtocolGuid, &Disk->BlockIo);
		if (EFI_ERROR(Status) && !EFI_ERROR(FirstError))
			FirstError = Status;
	}
	if (!Disk->DevicePathInstalled) {
		Status = CyanfsInstallDiskProtocol(Disk, &gEfiDevicePathProtocolGuid, Disk->DevicePath);
		if (EFI_ERROR(Status) && !EFI_ERROR(FirstError))
			FirstError = Status;
	}

	CyanfsRefreshDiskProtocols(Disk);
	if (Disk->BlockIoInstalled && Disk->DevicePathInstalled && Disk->ByChildOpened) {
		Disk->State = CYANFS_DISK_MANAGED;
		if (Reconnect)
			gBS->ConnectController(Disk->Handle, NULL, NULL, TRUE);
		return EFI_SUCCESS;
	}

	Disk->State = CYANFS_DISK_DEGRADED;
	return EFI_ERROR(FirstError) ? FirstError : EFI_DEVICE_ERROR;
}

static EFI_STATUS CyanfsUninstallDiskProtocols(CYANFS_DISK *Disk)
{
	EFI_STATUS Status;

	CyanfsRefreshDiskProtocols(Disk);
	if (Disk->BlockIoInstalled && Disk->DevicePathInstalled) {
		Status = gBS->UninstallMultipleProtocolInterfaces(Disk->Handle, &gEfiBlockIoProtocolGuid,
							 &Disk->BlockIo, &gEfiDevicePathProtocolGuid,
							 Disk->DevicePath, NULL);
	} else if (Disk->BlockIoInstalled) {
		Status = gBS->UninstallProtocolInterface(Disk->Handle, &gEfiBlockIoProtocolGuid, &Disk->BlockIo);
	} else if (Disk->DevicePathInstalled) {
		Status = gBS->UninstallProtocolInterface(Disk->Handle, &gEfiDevicePathProtocolGuid, Disk->DevicePath);
	} else {
		Status = EFI_SUCCESS;
	}

	CyanfsRefreshDiskProtocols(Disk);
	if (!EFI_ERROR(Status) && (Disk->BlockIoInstalled || Disk->DevicePathInstalled))
		return EFI_DEVICE_ERROR;
	return Status;
}

static EFI_STATUS CyanfsDestroyDisk(CYANFS_DISK *Disk)
{
	if (Disk->ByChildOpened || Disk->BlockIoInstalled || Disk->DevicePathInstalled)
		return EFI_DEVICE_ERROR;

	cyanfs_list_del(&Disk->Link);
	cyanfs_close(Disk->File);
	FreePool(Disk->DevicePath);
	FreePool(Disk);
	return EFI_SUCCESS;
}

static EFI_STATUS CyanfsValidateChildren(CYANFS_BACKEND *Backend, UINTN NumberOfChildren,
					 EFI_HANDLE *ChildHandleBuffer)
{
	UINTN Index, Previous;

	if (!ChildHandleBuffer)
		return EFI_INVALID_PARAMETER;

	for (Index = 0; Index < NumberOfChildren; Index++) {
		CYANFS_DISK *Disk;

		if (!ChildHandleBuffer[Index])
			return EFI_INVALID_PARAMETER;
		for (Previous = 0; Previous < Index; Previous++)
			if (ChildHandleBuffer[Previous] == ChildHandleBuffer[Index])
				return EFI_INVALID_PARAMETER;

		Disk = CyanfsFindChild(Backend, ChildHandleBuffer[Index]);
		if (!Disk || Disk->Backend != Backend || Disk->State == CYANFS_DISK_CREATING ||
		    Disk->State == CYANFS_DISK_STOPPING)
			return EFI_INVALID_PARAMETER;
	}
	return EFI_SUCCESS;
}

static EFI_STATUS CyanfsStopDisk(CYANFS_DISK *Disk)
{
	CYANFS_DISK_STATE PreviousState = Disk->State;
	EFI_STATUS Status, RecoveryStatus;

	Disk->State = CYANFS_DISK_STOPPING;
	Status = CyanfsIoFlush(&Disk->BlockIo);
	if (EFI_ERROR(Status)) {
		Disk->State = PreviousState;
		return Status;
	}

	Status = CyanfsCloseChildBlockIo(Disk);
	if (EFI_ERROR(Status)) {
		Disk->State = PreviousState;
		return Status;
	}

	Status = CyanfsUninstallDiskProtocols(Disk);
	if (!Disk->BlockIoInstalled && !Disk->DevicePathInstalled)
		return CyanfsDestroyDisk(Disk);

	RecoveryStatus = CyanfsRecoverDisk(Disk, TRUE);
	if (EFI_ERROR(Status))
		return Status;
	if (EFI_ERROR(RecoveryStatus))
		return RecoveryStatus;
	return EFI_DEVICE_ERROR;
}

static EFI_STATUS CyanfsStopChildren(CYANFS_BACKEND *Backend, UINTN NumberOfChildren,
				      EFI_HANDLE *ChildHandleBuffer)
{
	EFI_STATUS Status;
	BOOLEAN Failed = FALSE;
	UINTN Index;

	Status = CyanfsValidateChildren(Backend, NumberOfChildren, ChildHandleBuffer);
	if (EFI_ERROR(Status))
		return Status;

	for (Index = 0; Index < NumberOfChildren; Index++) {
		CYANFS_DISK *Disk = CyanfsFindChild(Backend, ChildHandleBuffer[Index]);

		Status = CyanfsStopDisk(Disk);
		if (EFI_ERROR(Status))
			Failed = TRUE;
	}
	return Failed ? EFI_DEVICE_ERROR : EFI_SUCCESS;
}

static VOID CyanfsRecoverDegradedChildren(CYANFS_BACKEND *Backend)
{
	struct cyanfs_list_head *Link, *Next;

	cyanfs_list_for_each_safe(Link, Next, &Backend->Children) {
		CYANFS_DISK *Disk = cyanfs_container_of(Link, CYANFS_DISK, Link);

		if (Disk->State == CYANFS_DISK_DEGRADED) {
			Disk->State = CYANFS_DISK_STOPPING;
			CyanfsRecoverDisk(Disk, TRUE);
		}
	}
}

EFI_STATUS EFIAPI CyanfsMap(IN EFI_CYANFS_PROTOCOL *Cyanfs, IN CYANFS_FILE_ID ID,
			    IN EFI_DEVICE_PATH *ParentDevicePath OPTIONAL, OUT EFI_DEVICE_PATH_PROTOCOL **DevicePath)
{
	CYANFS_BACKEND *Backend = ToBackend(Cyanfs);
	EFI_STATUS Status, RecoveryStatus;
	CYANFS_DISK *Disk;
	struct cyanfs_file *File;
	struct cyanfs_super_meta Meta;

	if (!DevicePath)
		return EFI_INVALID_PARAMETER;
	*DevicePath = NULL;

	Status = cyanfs_statfs(Backend->Super, &Meta);
	if (EFI_ERROR(Status))
		goto Exit;

	Status = cyanfs_open(Backend->Super, ID, !Backend->Disk->Media->ReadOnly, &File);
	if (EFI_ERROR(Status))
		goto Exit;

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
	Disk->State = CYANFS_DISK_CREATING;
	CYANFS_INIT_LIST_HEAD(&Disk->Link);
	cyanfs_list_add_tail(&Disk->Link, &Backend->Children);

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

	Disk->DevicePath = AppendDevicePathNode(ParentDevicePath, (EFI_DEVICE_PATH_PROTOCOL *)&Disk->DeviceNode);
	if (!Disk->DevicePath) {
		Status = EFI_OUT_OF_RESOURCES;
		goto DestroyDisk;
	}

	Status = gBS->InstallMultipleProtocolInterfaces(&Disk->Handle, &gEfiBlockIoProtocolGuid, &Disk->BlockIo,
							&gEfiDevicePathProtocolGuid, Disk->DevicePath, NULL);
	CyanfsRefreshDiskProtocols(Disk);
	if (EFI_ERROR(Status)) {
		if (!Disk->BlockIoInstalled && !Disk->DevicePathInstalled)
			goto DestroyDisk;
		RecoveryStatus = CyanfsRecoverDisk(Disk, FALSE);
		if (EFI_ERROR(RecoveryStatus))
			return RecoveryStatus;
		goto Commit;
	}
	if (!Disk->BlockIoInstalled || !Disk->DevicePathInstalled) {
		RecoveryStatus = CyanfsRecoverDisk(Disk, FALSE);
		if (EFI_ERROR(RecoveryStatus))
			return RecoveryStatus;
		goto Commit;
	}

	Status = CyanfsOpenChildBlockIo(Disk);
	if (EFI_ERROR(Status)) {
		CyanfsUninstallDiskProtocols(Disk);
		if (!Disk->BlockIoInstalled && !Disk->DevicePathInstalled)
			goto DestroyDisk;
		RecoveryStatus = CyanfsRecoverDisk(Disk, FALSE);
		if (EFI_ERROR(RecoveryStatus))
			return RecoveryStatus;
	}

Commit:
	Disk->State = CYANFS_DISK_MANAGED;
	*DevicePath = Disk->DevicePath;
	CYANFS_DEBUG("Register VDisk %p Handle %p Size: %llu", Disk, Disk->Handle, cyanfs_size(File));
	gBS->ConnectController(Disk->Handle, NULL, NULL, TRUE);
	return EFI_SUCCESS;

DestroyDisk:
	RecoveryStatus = CyanfsDestroyDisk(Disk);
	if (EFI_ERROR(RecoveryStatus))
		return RecoveryStatus;
	return Status;
CloseFile:
	cyanfs_close(File);
Exit:
	return Status;
}

static EFI_STATUS CyanfsCloseBlockIo(IN EFI_DRIVER_BINDING_PROTOCOL *This, IN EFI_HANDLE Controller)
{
	return gBS->CloseProtocol(Controller, &gEfiBlockIoProtocolGuid, This->DriverBindingHandle, Controller);
}

EFI_STATUS EFIAPI CyanfsDriverStart(IN EFI_DRIVER_BINDING_PROTOCOL *This, IN EFI_HANDLE Controller,
				    IN EFI_DEVICE_PATH_PROTOCOL *RemainingDevicePath)
{
	EFI_STATUS Status, CloseStatus;
	EFI_BLOCK_IO_PROTOCOL *Disk;
	EFI_CYANFS_PROTOCOL *Cyanfs;
	CYANFS_BACKEND *Backend;

	Status = gBS->OpenProtocol(Controller, &gEfiBlockIoProtocolGuid, (VOID **)&Disk, This->DriverBindingHandle,
				   Controller, EFI_OPEN_PROTOCOL_BY_DRIVER);
	if (EFI_ERROR(Status)) {
		goto Exit;
	}

	Backend = AllocatePool(sizeof(CYANFS_BACKEND));
	if (!Backend) {
		Status = EFI_OUT_OF_RESOURCES;
		goto CloseBlockIo;
	}

	Backend->Disk = Disk;
	Backend->Controller = Controller;
	Backend->DriverBindingHandle = This->DriverBindingHandle;
	CYANFS_INIT_LIST_HEAD(&Backend->Children);
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
CloseBlockIo:
	CloseStatus = CyanfsCloseBlockIo(This, Controller);
	if (EFI_ERROR(CloseStatus))
		ASSERT_EFI_ERROR(CloseStatus);
Exit:
	return Status;
}

EFI_STATUS EFIAPI CyanfsDriverStop(IN EFI_DRIVER_BINDING_PROTOCOL *This, IN EFI_HANDLE Controller,
				   IN UINTN NumberOfChildren, IN EFI_HANDLE *ChildHandleBuffer)
{
	EFI_STATUS Status, CloseStatus;
	EFI_CYANFS_PROTOCOL *Cyanfs;
	CYANFS_BACKEND *Backend;

	CYANFS_DEBUG("BackendDiskHandle", Controller);
	Status = gBS->OpenProtocol(Controller, &gEfiCyanfsProtocolGuid, (VOID **)&Cyanfs, This->DriverBindingHandle,
				   Controller, EFI_OPEN_PROTOCOL_GET_PROTOCOL);
	if (EFI_ERROR(Status))
		return EFI_INVALID_PARAMETER;

	Backend = ToBackend(Cyanfs);
	if (NumberOfChildren) {
		Status = CyanfsStopChildren(Backend, NumberOfChildren, ChildHandleBuffer);
		CloseStatus = gBS->CloseProtocol(Controller, &gEfiCyanfsProtocolGuid, This->DriverBindingHandle,
						 Controller);
		if (EFI_ERROR(Status)) {
			if (EFI_ERROR(CloseStatus))
				ASSERT_EFI_ERROR(CloseStatus);
			return Status;
		}
		return CloseStatus;
	}

	if (!cyanfs_list_empty(&Backend->Children)) {
		CyanfsRecoverDegradedChildren(Backend);
		CloseStatus = gBS->CloseProtocol(Controller, &gEfiCyanfsProtocolGuid, This->DriverBindingHandle,
						 Controller);
		if (EFI_ERROR(CloseStatus))
			ASSERT_EFI_ERROR(CloseStatus);
		return EFI_DEVICE_ERROR;
	}

	Status = CyanfsFlushMetadata(Backend, EFI_SUCCESS);
	CloseStatus = gBS->CloseProtocol(Controller, &gEfiCyanfsProtocolGuid, This->DriverBindingHandle, Controller);
	if (EFI_ERROR(Status)) {
		if (EFI_ERROR(CloseStatus))
			ASSERT_EFI_ERROR(CloseStatus);
		return Status;
	}
	if (EFI_ERROR(CloseStatus))
		return CloseStatus;

	Status = gBS->UninstallProtocolInterface(Controller, &gEfiCyanfsProtocolGuid, Cyanfs);
	if (EFI_ERROR(Status))
		return Status;

	CloseStatus = CyanfsCloseBlockIo(This, Controller);
	if (EFI_ERROR(CloseStatus))
		ASSERT_EFI_ERROR(CloseStatus);
	cyanfs_super_close(Backend->Super);
	FreePool(Backend);
	return CloseStatus;
}

EFI_STATUS EFIAPI CyanfsDriverSupported(IN EFI_DRIVER_BINDING_PROTOCOL *This, IN EFI_HANDLE Controller,
					IN EFI_DEVICE_PATH_PROTOCOL *RemainingDevicePath)
{
	EFI_STATUS Status, CloseStatus;
	EFI_BLOCK_IO_PROTOCOL *Disk;
	UINT8 *Buffer;
	UINT64 DiskSize;
	struct cyanfs_super_header Header;

	Status = gBS->OpenProtocol(Controller, &gEfiBlockIoProtocolGuid, (VOID **)&Disk, This->DriverBindingHandle,
				   Controller, EFI_OPEN_PROTOCOL_BY_DRIVER);
	if (EFI_ERROR(Status)) {
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
	CloseStatus = CyanfsCloseBlockIo(This, Controller);
	if (EFI_ERROR(CloseStatus))
		return CloseStatus;
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

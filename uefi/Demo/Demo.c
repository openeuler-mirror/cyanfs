#include <Uefi.h>
#include <Protocol/Cyanfs.h>
#include <Library/UefiLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/DevicePathLib.h>

EFI_STATUS RunImage(EFI_HANDLE Device, CHAR16 *FilePath)
{
	EFI_STATUS Status;
	EFI_HANDLE LoadedDriverHandle;
	EFI_DEVICE_PATH_PROTOCOL *FullFilePath;

	FullFilePath = FileDevicePath(Device, FilePath);
	Status = gBS->LoadImage(FALSE, gImageHandle, FullFilePath, NULL, 0, &LoadedDriverHandle);
	if (!EFI_ERROR(Status)) {
		Status = gBS->StartImage(LoadedDriverHandle, NULL, NULL);
	}
	return Status;
}

EFI_STATUS EFIAPI UefiMain(IN EFI_HANDLE ImageHandle, IN EFI_SYSTEM_TABLE *SystemTable)
{
	EFI_STATUS Status;
	EFI_CYANFS_PROTOCOL *Cyanfs;
	CYANFS_FILE_META Meta = CyanfsFileMetaInit;
	CYANFS_FILE_ID ID = 0;
	EFI_DEVICE_PATH_PROTOCOL *DevicePath;
	UINT32 Index;
	UINTN NoHandles;
	EFI_HANDLE *Handles;

	Status = gBS->LocateHandleBuffer(ByProtocol, &gEfiSimpleFileSystemProtocolGuid, NULL, &NoHandles, &Handles);
	if (EFI_ERROR(Status)) {
		AsciiPrint("Simple file system unsupported\n");
		Status = EFI_UNSUPPORTED;
		goto Exit;
	}
	for (Index = 0; Index < NoHandles; Index++) {
		RunImage(Handles[Index], L"CyanfsDxe.efi");
	}
	if (NoHandles)
		FreePool(Handles);

	Status = gBS->LocateHandleBuffer(AllHandles, NULL, NULL, &NoHandles, &Handles);
	if (EFI_ERROR(Status)) {
		goto Exit;
	}
	for (Index = 0; Index < NoHandles; Index++) {
		Status = gBS->ConnectController(Handles[Index], NULL, NULL, TRUE);
	}
	if (NoHandles)
		FreePool(Handles);

	Status = gBS->LocateProtocol(&gEfiCyanfsProtocolGuid, NULL, (void **)&Cyanfs);
	if (EFI_ERROR(Status)) {
		Status = EFI_OUT_OF_RESOURCES;
		goto Exit;
	}

	do {
		Status = Cyanfs->List(Cyanfs, &Meta);

		if (EFI_ERROR(Status))
			break;
		AsciiPrint("list filename: %a\n", &Meta.Name);
		ID = Meta.ID;
	} while (1);

	if (!ID) {
		AsciiPrint("Cannot find cyanfs file.\n");
		Status = EFI_NOT_FOUND;
		goto Exit;
	}

	AsciiPrint("Map file %llu\n", ID);
	Status = Cyanfs->Map(Cyanfs, ID, NULL, &DevicePath);
	AsciiPrint("Map Status %llx\n", Status);

	Status = gBS->LocateHandleBuffer(ByProtocol, &gEfiSimpleFileSystemProtocolGuid, NULL, &NoHandles, &Handles);
	if (EFI_ERROR(Status)) {
		AsciiPrint("Simple file system unsupported\n");
		Status = EFI_UNSUPPORTED;
		goto Exit;
	}
	for (Index = 0; Index < NoHandles; Index++) {
		RunImage(Handles[Index], L"EFI\\ubuntu\\shimx64.efi");
		RunImage(Handles[Index], L"EFI\\debian\\shimx64.efi");
	}
	if (NoHandles)
		FreePool(Handles);

	Status = EFI_SUCCESS;

Exit:
	return Status;
}

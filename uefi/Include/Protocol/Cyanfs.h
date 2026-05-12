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
#ifndef __CYANFS_PROTOCOL_HEADER__
#define __CYANFS_PROTOCOL_HEADER__

#include <Uefi.h>
#include <Protocol/DevicePath.h>

#define EFI_CYANFS_PROTOCOL_GUID { 0x12ab0cf2, 0x7a9a, 0x289b, { 0xa2, 0xf1, 0x34, 0xbb, 0xc9, 0x0a, 0x24, 0x3c } };

extern EFI_GUID gEfiCyanfsProtocolGuid;

typedef struct _EFI_CYANFS_PROTOCOL EFI_CYANFS_PROTOCOL;

typedef UINT64 CYANFS_FILE_ID;

typedef struct {
	UINT8 Data[127];
	UINT8 Zero;
} CYANFS_FILE_NAME;

typedef struct {
	CYANFS_FILE_ID ID;
	CYANFS_FILE_ID ParentID;
	CYANFS_FILE_NAME Name;
	UINT64 Size;
	UINT32 Extents;
	UINT32 Children;
} CYANFS_FILE_META;

typedef struct {
	UINT32 Data[4];
} CYANFS_UUID;

typedef struct {
	CYANFS_UUID UUID;
	UINT32 TotalExtents;
	UINT32 FreeExtents;
	UINT32 JournalExtents;
	UINT32 DataExtents;
	UINT32 ReservedExtents;
	UINT64 Size;
	UINT32 Files;
} CYANFS_SUPER_META;

typedef EFI_STATUS(EFIAPI *EFI_CYANFS_STATFS)(IN EFI_CYANFS_PROTOCOL *Cyanfs, OUT CYANFS_SUPER_META *Meta);
const static CYANFS_FILE_META CyanfsFileMetaInit = { 0 };
typedef EFI_STATUS(EFIAPI *EFI_CYANFS_LIST)(IN EFI_CYANFS_PROTOCOL *Cyanfs, IN OUT CYANFS_FILE_META *Meta);
typedef EFI_STATUS(EFIAPI *EFI_CYANFS_LOOKUP)(IN EFI_CYANFS_PROTOCOL *Cyanfs, IN CYANFS_FILE_NAME Name,
					      OUT CYANFS_FILE_META *Meta);
typedef EFI_STATUS(EFIAPI *EFI_CYANFS_CREATE)(IN EFI_CYANFS_PROTOCOL *Cyanfs, IN CYANFS_FILE_NAME Name,
					      OUT CYANFS_FILE_ID *ID);
typedef EFI_STATUS(EFIAPI *EFI_CYANFS_FORK)(IN EFI_CYANFS_PROTOCOL *Cyanfs, IN CYANFS_FILE_ID From,
					    IN CYANFS_FILE_NAME Name, OUT CYANFS_FILE_ID *ID);
typedef EFI_STATUS(EFIAPI *EFI_CYANFS_RENAME)(IN EFI_CYANFS_PROTOCOL *Cyanfs, IN CYANFS_FILE_ID From,
					      IN CYANFS_FILE_NAME Name);
typedef EFI_STATUS(EFIAPI *EFI_CYANFS_TRUNCATE)(IN EFI_CYANFS_PROTOCOL *Cyanfs, IN CYANFS_FILE_ID ID, IN UINT64 Size);
typedef EFI_STATUS(EFIAPI *EFI_CYANFS_DELETE)(IN EFI_CYANFS_PROTOCOL *Cyanfs, IN CYANFS_FILE_ID ID);
typedef EFI_STATUS(EFIAPI *EFI_CYANFS_MAP)(IN EFI_CYANFS_PROTOCOL *Cyanfs, IN CYANFS_FILE_ID ID,
					   IN EFI_DEVICE_PATH *ParentDevicePath OPTIONAL,
					   OUT EFI_DEVICE_PATH_PROTOCOL **DevicePath);

struct _EFI_CYANFS_PROTOCOL {
	EFI_CYANFS_STATFS Statfs;
	EFI_CYANFS_LIST List;
	EFI_CYANFS_LOOKUP Lookup;
	EFI_CYANFS_CREATE Create;
	EFI_CYANFS_FORK Fork;
	EFI_CYANFS_RENAME Rename;
	EFI_CYANFS_TRUNCATE Truncate;
	EFI_CYANFS_DELETE Delete;
	EFI_CYANFS_MAP Map;
};

typedef struct {
	EFI_DEVICE_PATH_PROTOCOL Header;
	CYANFS_UUID UUID;
	CYANFS_FILE_ID FileID;
} MEDIA_CYANFS_DISK_DEVICE_PATH;

#endif

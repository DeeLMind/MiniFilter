#include <fltKernel.h>
#include <dontuse.h>
#include <suppress.h>
#include <ntddscsi.h>		

FLT_PREOP_CALLBACK_STATUS
NPPreCreate(
__inout PFLT_CALLBACK_DATA Data,
__in PCFLT_RELATED_OBJECTS FltObjects,
__deref_out_opt PVOID *CompletionContext
);

FLT_PREOP_CALLBACK_STATUS
NPPreSetInformation(
__inout PFLT_CALLBACK_DATA Data,
__in PCFLT_RELATED_OBJECTS FltObjects,
__deref_out_opt PVOID *CompletionContext
);

NTSTATUS Unload(__in FLT_FILTER_UNLOAD_FLAGS Flags);

PFLT_FILTER m_Filter;

PFLT_PORT m_ServerPort;

PFLT_PORT m_ClientPort;

LIST_ENTRY m_ListHead;											//��������Ҫ���˵��ļ���������

KSPIN_LOCK m_SpinLock;											//������ϵ���

LIST_ENTRY x_ListHead;											//�������淵���ļ���Ϣ������

KSPIN_LOCK x_SpinLock;											//������ϵĵ���

typedef struct _FileName
{
	LIST_ENTRY ListEntry;
	WCHAR Name[40];												//�����ã��ļ����ٶ�������19���ַ�
}FILENAME,*PFILENAME;

typedef struct _ZTYMESSAGE
{
	ULONG Flag;													//��ʾ�Ǵ�������ɾ������������������0��ɾ��1��������2
	WCHAR PATH[300];											
}ZTYMESSAGE, *PZTYMESSAGE;

typedef struct _FILEPATH
{
	LIST_ENTRY ListEntry;
	ZTYMESSAGE Message;
}FILEPATH,*PFILEPATH;

CONST FLT_OPERATION_REGISTRATION CallBack[] = {
	{
		IRP_MJ_CREATE,
		0,
		NPPreCreate,
		NULL
	},
	{
		IRP_MJ_SET_INFORMATION,							//��������ɾ��������SET_INFORMATION��
		0,
		NPPreSetInformation,
		NULL
	},
	{ IRP_MJ_OPERATION_END }
};

CONST FLT_REGISTRATION FilterRegistration =
{
	sizeof(FLT_REGISTRATION),
	FLT_REGISTRATION_VERSION,
	NULL,
	NULL,
	CallBack,
	Unload,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL
};

BOOLEAN JudgeLengthOfMessage()
{
	KIRQL irql;

	ULONG i = 0;

	PLIST_ENTRY TempList;

	KeAcquireSpinLock(&x_SpinLock, &irql);

	TempList = x_ListHead.Blink;

	while (TempList != &x_ListHead)
	{
		++i;

		TempList = TempList->Blink;
	}

	KeReleaseSpinLock(&x_SpinLock, irql);

	if (i < 1000)
		return TRUE;

	KdPrint(("����50����Ϣ������������ˣ�\n"));

	return FALSE;
}

NTSTATUS JudgeFileExist(PUNICODE_STRING FileName)						//�ж��ļ��Ƿ����
{
	NTSTATUS status;

	HANDLE FileHandle;														//�������Open_IF����ȥ�ж��Ƿ���ڣ���������ھͼ������Ŀ¼��

	IO_STATUS_BLOCK IoBlock;

	OBJECT_ATTRIBUTES ObjectAttributes;

	InitializeObjectAttributes(&ObjectAttributes, FileName, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);

	status = ZwCreateFile(&FileHandle, GENERIC_ALL, &ObjectAttributes, &IoBlock, NULL, FILE_ATTRIBUTE_NORMAL, FILE_SHARE_READ, FILE_OPEN, FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT, NULL, 0);

	if (NT_SUCCESS(status))
		ZwClose(FileHandle);

	return status;
}

BOOLEAN JudgeFile(PUNICODE_STRING FileName)								//�ж��ļ����Ƿ���������Ҫ���˵��ļ���
{
	ULONG i = 0;

	ULONG j;

	KIRQL irql;

	PFILENAME Filter_FileName;

	PLIST_ENTRY TempList;

	TempList = m_ListHead.Blink;

	while (TempList != &m_ListHead)
	{
		Filter_FileName = (PFILENAME)TempList;

		i = 0;

		while (i < (FileName->Length / 2))
		{
			j = 0;

			while (Filter_FileName->Name[j] != L'\0' && (i + j) < FileName->Length / 2)
			{
				if (Filter_FileName->Name[j] != FileName->Buffer[i + j])
					break;
				++j;
			}

			if (Filter_FileName->Name[j] == L'\0')
				return TRUE;

			++i;
		}

		TempList = TempList->Blink;
	}

	return FALSE;
}

FLT_PREOP_CALLBACK_STATUS m_ReNameFile(PFLT_CALLBACK_DATA Data, PCFLT_RELATED_OBJECTS FltObjects)
{
	NTSTATUS status;

	PFILE_RENAME_INFORMATION pReNameInfo;

	PFLT_FILE_NAME_INFORMATION NameInfo;

	PFILEPATH FilePath;

	ULONG i = 0;

	pReNameInfo = (PFILE_RENAME_INFORMATION)Data->Iopb->Parameters.SetFileInformation.InfoBuffer;

	status = FltGetDestinationFileNameInformation(FltObjects->Instance,
		Data->Iopb->TargetFileObject, 
		pReNameInfo->RootDirectory, 
		pReNameInfo->FileName, 
		pReNameInfo->FileNameLength, 
		FLT_FILE_NAME_NORMALIZED,
		&NameInfo);

	if (!NT_SUCCESS(status))
	{
		KdPrint(("Get Destination Name Fail!\n"));
		return FLT_PREOP_SUCCESS_NO_CALLBACK;
	}

	if (JudgeFile(&NameInfo->Name))																//��ֹ���ֶ�Ӧ���Ƶ��ļ���							
	{
		Data->IoStatus.Status = STATUS_ACCESS_DENIED;
		Data->IoStatus.Information = 0;
		FltReleaseFileNameInformation(NameInfo);
		return FLT_PREOP_COMPLETE;
	}

	if (!JudgeLengthOfMessage())
	{
		FltReleaseFileNameInformation(NameInfo);
		return FLT_PREOP_SUCCESS_NO_CALLBACK;
	}

	FilePath = (PFILEPATH)ExAllocatePoolWithTag(NonPagedPool, sizeof(FILEPATH), 'ytz');

	if (FilePath != NULL)
	{
		FilePath->Message.Flag = 2;
		
		while (i < NameInfo->Name.Length / 2)
		{
			FilePath->Message.PATH[i] = NameInfo->Name.Buffer[i];
			++i;
		}

		FilePath->Message.PATH[NameInfo->Name.Length / 2] = L'\0';

		ExInterlockedInsertTailList(&x_ListHead, (PLIST_ENTRY)FilePath, &x_SpinLock);

		KdPrint(("Rename:%ws\n", FilePath->Message.PATH));
	}

	//KdPrint(("ReName:%wZ\n", &NameInfo->Name));

	FltReleaseFileNameInformation(NameInfo);

	return FLT_PREOP_SUCCESS_NO_CALLBACK;
}

FLT_PREOP_CALLBACK_STATUS m_DeleteFile(PFLT_CALLBACK_DATA Data, PCFLT_RELATED_OBJECTS FltObjects)
{
	NTSTATUS status;

	BOOLEAN isDir;

	PFLT_FILE_NAME_INFORMATION NameInfo;

	PFILEPATH FilePath;

	ULONG i = 0;

	status = FltIsDirectory(FltObjects->FileObject, FltObjects->Instance, &isDir);

	if (!NT_SUCCESS(status))
	{
		return FLT_PREOP_SUCCESS_NO_CALLBACK;
	}

	if (isDir)
		return FLT_PREOP_SUCCESS_NO_CALLBACK;					//�������������ļ��У��Ͳ�ȥ������

	status = FltGetFileNameInformation(Data, FLT_FILE_NAME_NORMALIZED
		| FLT_FILE_NAME_QUERY_DEFAULT, &NameInfo);

	if (!NT_SUCCESS(status))
	{
		KdPrint(("Query Name Fail!\n"));
		return FLT_PREOP_SUCCESS_NO_CALLBACK;
	}

	FltParseFileNameInformation(NameInfo);

	if (!NT_SUCCESS(status))
	{
		KdPrint(("Parse Name Fail!\n"));
		return FLT_PREOP_SUCCESS_NO_CALLBACK;
	}

	if (!JudgeLengthOfMessage())
	{
		FltReleaseFileNameInformation(NameInfo);
		return FLT_PREOP_SUCCESS_NO_CALLBACK;
	}

	FilePath = (PFILEPATH)ExAllocatePoolWithTag(NonPagedPool, sizeof(FILEPATH), 'ytz');

	if (FilePath != NULL)
	{
		FilePath->Message.Flag = 1;

		while (i < NameInfo->Name.Length / 2)
		{
			FilePath->Message.PATH[i] = NameInfo->Name.Buffer[i];
			++i;
		}

		FilePath->Message.PATH[NameInfo->Name.Length / 2] = L'\0';

		ExInterlockedInsertTailList(&x_ListHead, (PLIST_ENTRY)FilePath, &x_SpinLock);

		KdPrint(("Delete:%ws\n", FilePath->Message.PATH));
	}

	//KdPrint(("Delete:%wZ\n", &NameInfo->Name));

	FltReleaseFileNameInformation(NameInfo);

	return FLT_PREOP_SUCCESS_NO_CALLBACK;
}

FLT_PREOP_CALLBACK_STATUS
NPPreSetInformation(
__inout PFLT_CALLBACK_DATA Data,
__in PCFLT_RELATED_OBJECTS FltObjects,
__deref_out_opt PVOID *CompletionContext
)
{
	if (Data->Iopb->Parameters.SetFileInformation.FileInformationClass == FileRenameInformation)						//����������
		return m_ReNameFile(Data,FltObjects);
	else if (Data->Iopb->Parameters.SetFileInformation.FileInformationClass == FileDispositionInformation)				//ɾ������
		return m_DeleteFile(Data, FltObjects);
	else
		return FLT_PREOP_SUCCESS_NO_CALLBACK;																			//�����������ܣ�ֱ�ӷ���SUCCESS

}

FLT_PREOP_CALLBACK_STATUS
NPPreCreate(
__inout PFLT_CALLBACK_DATA Data,
__in PCFLT_RELATED_OBJECTS FltObjects,
__deref_out_opt PVOID *CompletionContext
)
{
	NTSTATUS status;

	ULONG CreatePosition;

	ULONG Position;

	PFLT_FILE_NAME_INFORMATION NameInfo;

	PFILEPATH FilePath;

	ULONG i = 0;

	Position = Data->Iopb->Parameters.Create.Options;

	CreatePosition = (Data->Iopb->Parameters.Create.Options >> 24) & 0xFF;	//�����������鷢�֣�Create.Options�ķֲ��������ӵģ���һ�ֽ���create disposition values�����������ֽ���option flags

	if (Position & FILE_DIRECTORY_FILE)
		return FLT_PREOP_SUCCESS_NO_CALLBACK;								//����������ļ���ѡ��ֱ�ӷ���

	if (CreatePosition == FILE_OPEN)
		return FLT_PREOP_SUCCESS_NO_CALLBACK;								//�����FILE_OPEN���ļ���ֱ�ӷ���

	status = FltGetFileNameInformation(Data, FLT_FILE_NAME_NORMALIZED | FLT_FILE_NAME_QUERY_DEFAULT, &NameInfo);

	if (!NT_SUCCESS(status))
	{
		KdPrint(("Query Name Fail!\n"));
		return FLT_PREOP_SUCCESS_NO_CALLBACK;
	}

	status = FltParseFileNameInformation(NameInfo);

	if (!NT_SUCCESS(status))
	{
		KdPrint(("Parse Name Fail!\n"));
		return FLT_PREOP_SUCCESS_NO_CALLBACK;
	}

	if (JudgeFile(&NameInfo->Name))
	{
		Data->IoStatus.Status = STATUS_ACCESS_DENIED;
		Data->IoStatus.Information = 0;
		FltReleaseFileNameInformation(NameInfo);
		return FLT_PREOP_COMPLETE;
	}

	if (CreatePosition == FILE_OPEN_IF || CreatePosition == FILE_OVERWRITE_IF)						//������������****IF�������������****�����򴴽���������Ҫ���ж��Ƿ���ڣ�����������ڹ��˷�Χ��������ǹ��˷�Χ���ˡ�
	{
		//KdPrint(("FILE_OPEN_IF OR FILE_OVERWRITE_IF\n"));
		if (NT_SUCCESS(JudgeFileExist(&NameInfo->Name)))
		{
			//KdPrint(("�ļ��Ѿ����ڣ�\n"));
			FltReleaseFileNameInformation(NameInfo);
			return FLT_PREOP_SUCCESS_NO_CALLBACK;
		}
	}

	if (!JudgeLengthOfMessage())
	{
		FltReleaseFileNameInformation(NameInfo);
		return FLT_PREOP_SUCCESS_NO_CALLBACK;
	}

	FilePath = (PFILEPATH)ExAllocatePoolWithTag(NonPagedPool, sizeof(FILEPATH), 'ytz');

	if (FilePath != NULL)
	{
		FilePath->Message.Flag = 0;

		while (i < NameInfo->Name.Length / 2)
		{
			FilePath->Message.PATH[i] = NameInfo->Name.Buffer[i];
			++i;
		}

		FilePath->Message.PATH[i] = L'\0';

		ExInterlockedInsertTailList(&x_ListHead, (PLIST_ENTRY)FilePath, &x_SpinLock);

		KdPrint(("Create:%ws\n", FilePath->Message.PATH));
	}

	//KdPrint(("Create:%wZ\n", &NameInfo->Name));

	FltReleaseFileNameInformation(NameInfo);

	return FLT_PREOP_SUCCESS_NO_CALLBACK;
}

NTSTATUS
m_Connect(
__in PFLT_PORT ClientPort,
__in PVOID ServerPortCookie,
__in_bcount(SizeOfContext) PVOID ConnectionContext,
__in ULONG SizeOfContext,
__deref_out_opt PVOID *ConnectionCookie
)
{
	if (ClientPort == NULL)
	{
		KdPrint(("ClinetPort is NULL!\n"));
		return STATUS_UNSUCCESSFUL;
	}

	m_ClientPort = ClientPort;

	return STATUS_SUCCESS;
}

VOID
m_Disconnect(
__in_opt PVOID ConnectionCookie
)
{
	FltCloseClientPort(m_Filter, &m_ClientPort);
}

NTSTATUS
m_Message(
__in_opt PVOID PortCookie,
__in_bcount_opt(InputBufferLength) PVOID InputBuffer,
__in ULONG InputBufferLength,
__out_bcount_part_opt(OutputBufferLength, *ReturnOutputBufferLength) PVOID OutputBuffer,
__in ULONG OutputBufferLength,
__out PULONG ReturnOutputBufferLength
)
{
	WCHAR *InputMessage;

	PFILENAME FileName;

	PFILEPATH FilePath;

	ULONG i = 0;

	if (InputBufferLength != 1)
	{
		InputMessage = (WCHAR *)InputBuffer;

		FileName = (PFILENAME)ExAllocatePoolWithTag(NonPagedPool, sizeof(FILENAME), 'ytz');

		if (FileName == NULL)
		{
			KdPrint(("�����ڴ�ʧ�ܣ�\n"));
			return STATUS_UNSUCCESSFUL;
		}

		while (i < InputBufferLength / 2)
		{
			FileName->Name[i] = InputMessage[i];
			++i;
		}

		KdPrint(("Length:%d\n", i));

		KdPrint(("%ws\n", FileName->Name));

		ExInterlockedInsertTailList(&m_ListHead, (PLIST_ENTRY)FileName,&m_SpinLock);
	}
	else if (OutputBufferLength != 0)
	{
		FilePath = (PFILEPATH)ExInterlockedRemoveHeadList(&x_ListHead, &x_SpinLock);

		if (FilePath == NULL)
			return STATUS_UNSUCCESSFUL;									//����û����

		if (OutputBufferLength != sizeof(ZTYMESSAGE))
			return STATUS_UNSUCCESSFUL;									//Ӧ�ò㻺��������

		RtlCopyMemory(OutputBuffer, &FilePath->Message, sizeof(ZTYMESSAGE));

		ExFreePoolWithTag(FilePath, 'ytz');
	}

	return  STATUS_SUCCESS;
}

NTSTATUS Unload(__in FLT_FILTER_UNLOAD_FLAGS Flags)
{
	PFILENAME m_list;

	PFILEPATH x_list;

	KdPrint(("Unload Success!\n"));

	FltCloseCommunicationPort(m_ServerPort);

	FltUnregisterFilter(m_Filter);

	m_list = (PFILENAME)ExInterlockedRemoveHeadList(&m_ListHead, &m_SpinLock);

	while (m_list != NULL)
	{
		ExFreePoolWithTag(m_list, 'ytz');
		m_list = (PFILENAME)ExInterlockedRemoveHeadList(&m_ListHead, &m_SpinLock);
	}

	x_list = (PFILEPATH)ExInterlockedRemoveHeadList(&x_ListHead, &x_SpinLock);

	while (x_list != NULL)
	{
		ExFreePoolWithTag(x_list, 'ytz');
		x_list = (PFILENAME)ExInterlockedRemoveHeadList(&x_ListHead, &x_SpinLock);
	}

	return STATUS_SUCCESS;
}

NTSTATUS InitFltFilter(PDRIVER_OBJECT DriverObject)
{
	NTSTATUS status;

	status = FltRegisterFilter(DriverObject, &FilterRegistration, &m_Filter);

	if (!NT_SUCCESS(status))
	{
		KdPrint(("Register Filter UnSuccess!\n"));
		return STATUS_UNSUCCESSFUL;
	}

	status = FltStartFiltering(m_Filter);

	if (!NT_SUCCESS(status))
	{
		KdPrint(("Start Filter UnSuccess!\n"));
		FltUnregisterFilter(m_Filter);
		return STATUS_UNSUCCESSFUL;
	}

	return STATUS_SUCCESS;
}

NTSTATUS InitcommunicationPort()
{
	PSECURITY_DESCRIPTOR SecurityDes;

	OBJECT_ATTRIBUTES ObjectAttributes;

	UNICODE_STRING PortName;

	NTSTATUS status;

	status = FltBuildDefaultSecurityDescriptor(&SecurityDes, FLT_PORT_ALL_ACCESS);

	if (!NT_SUCCESS(status))
	{
		KdPrint(("BuilDefaultSecurityDescriptor Fail!Erorr Code is :%x \n", status));
		return STATUS_UNSUCCESSFUL;
	}

	RtlInitUnicodeString(&PortName, L"\\ztyPort");

	InitializeObjectAttributes(&ObjectAttributes,
		&PortName,
		OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE,
		NULL,
		SecurityDes
		);

	status = FltCreateCommunicationPort(m_Filter,
		&m_ServerPort,
		&ObjectAttributes,
		NULL,
		m_Connect,																//MiniConnect
		m_Disconnect,															//MiniDisConnect
		m_Message,																//MiniMessage
		1																		//�����������
		);

	if (!NT_SUCCESS(status))
	{
		KdPrint(("CreateCommuicationPort Fail!\n"));
		FltFreeSecurityDescriptor(SecurityDes);
		return STATUS_UNSUCCESSFUL;
	}

	FltFreeSecurityDescriptor(SecurityDes);

	return STATUS_SUCCESS;
}

NTSTATUS Init()
{
	KdPrint(("Entry Driver!\n"));

	InitializeListHead(&m_ListHead);

	KeInitializeSpinLock(&m_SpinLock);

	InitializeListHead(&x_ListHead);

	KeInitializeSpinLock(&x_SpinLock);
}

NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegString)
{
	Init();

	if (!NT_SUCCESS(InitFltFilter(DriverObject)))
		return STATUS_UNSUCCESSFUL;

	if (!NT_SUCCESS(InitcommunicationPort()))
		return STATUS_UNSUCCESSFUL;

	return STATUS_SUCCESS;
}
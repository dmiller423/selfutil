// selfutil.cpp : 
//

#include "pch.h"

#include "selfutil.h"


void print_usage()
{
	printf("selfutil [options] input_file \n");
}


int main(int argc, char *argv[])
{
	vector<string> args;

	if(argc<2) {
		print_usage();
		exit(0);
	}

	for(int i=1; i<argc; i++)
		args.push_back(argv[i]);

	string& fileName = args.back();


	SelfUtil util(fileName);

	size_t newSize = fileName.rfind('.');	// *FIXME* if this is already named .elf it will save to same file!
	string savePath = fileName;
	savePath.resize(newSize);
	savePath += ".elf";

	if(!util.SaveToELF(savePath))
		printf("Error, Save to ELF failed!\n");


	return 0;
}








bool SelfUtil::Load(string filePath)
{
	if(!filesystem::exists(filePath)) {
		printf("Failed to find file: \"%s\" \n", filePath.c_str());
		return false;
	}

	size_t fileSize = (size_t)filesystem::file_size(filePath);
	data.resize(fileSize);

	FILE *f=nullptr;
	fopen_s(&f, filePath.c_str(), "rb");
	if(f) {
		fread(&data[0], 1,fileSize, f);
		fclose(f);
	}
	else {
		printf("Failed to open file: \"%s\" \n", filePath.c_str());
		return false;
	}

	return Parse();
}

bool SelfUtil::SaveToELF(string savePath)
{
	printf("\n\nSaveToELF(\"%s\")\n",savePath.c_str());
	
	Elf64_Off first=777777777, last=0;
	size_t saveSize=0;
	for (auto ph : phdrs) {
		last =  std::max(last, ph->p_offset);
		if (0 != ph->p_offset)
			first =  std::min(first, ph->p_offset);
		saveSize = std::max(saveSize, (size_t)(ph->p_offset+ph->p_filesz));
	}
	saveSize = AlignUp<size_t>(saveSize, PS4_PAGE_SIZE);

	printf("Save Size: %d bytes (0x%X) \n", saveSize, saveSize);
	printf("first , last : %llX , %llX \n", first, last);

	save.clear();
	save.resize(saveSize);

	u8* pd = &save[0];
	memset(pd, 0, saveSize);


#if 1
	memcpy(pd, eHead, first);	// just copy everything from head to what should be first seg offs //
#else
	memcpy(pd, eHead, sizeof(elf64_hdr));

#if 0	// and hope it took care of all of this 
  Elf64_Half e_ehsize;

  Elf64_Half e_phentsize;
  Elf64_Half e_phnum;

  Elf64_Half e_shentsize;
  Elf64_Half e_shnum;
#endif
#endif

	for (auto ee : entries)
	{
		if(0==(ee->props & 0x800))
			continue;

		unat phIdx = (ee->props >> 20) & 0xFFF;

		Elf64_Phdr* ph = phdrs.at(phIdx);

		if (ph->p_filesz != ee->memSz)
			printf("idx: %d SEGMENT size: %d != phdr size: %d \n", phIdx, ee->memSz, ph->p_filesz);

		void *srcp = (void*)((unat)&data[0] + ee->offs);
		void *dstp = (void*)((unat)pd + ph->p_offset);
		memcpy(dstp, srcp, ee->fileSz);
	}



	FILE *f=nullptr;
	fopen_s(&f, savePath.c_str(), "wb");
	if(f) {
		fwrite(pd, 1,saveSize, f);
		fclose(f);
	}
	else return false;

	return true;
}






bool SelfUtil::Parse()
{
	if (data.size() < PS4_PAGE_SIZE) {
		printf("Bad file size! (%d)\n", data.size());
		return false;
	}

	seHead = (Self_Hdr*)&data[0];

	if (SELF_MAGIC != seHead->magic) {
		printf("Invalid Magic! (0x%08X)\n", seHead->magic);
		return false;
	}
		
	entries.clear();
	for (unat seIdx=0; seIdx<seHead->num_entries; seIdx++)
	{
		entries.push_back(&((Self_Entry*)&data[0])[1 + seIdx]);

		const auto se = entries.back();

		printf("Segment[%02d] P:%08X ",
			seIdx, se->props); 
			
		printf(" (id: %X) ", (se->props>>20));
		printf("@ 0x%016llX +%llX   (mem: %llX)\n",
			se->offs, se->fileSz, se->memSz);
	}
		
	elfHOffs = (1 + seHead->num_entries) * 0x20;
		
	eHead = (elf64_hdr*)(&data[0] + elfHOffs);

	if (!TestIdent()) {
		printf("Elf e_ident invalid!\n");
		return false;
	}

		
	for (unat phIdx=0; phIdx<eHead->e_phnum; phIdx++)
		phdrs.push_back(&((Elf64_Phdr*)(&data[0] + elfHOffs + eHead->e_phoff))[phIdx]);


	return true;
}






bool SelfUtil::TestIdent()
{
	if (ELF_MAGIC != ((u32*)eHead->e_ident)[0]) {
		printf("File is invalid! e_ident magic: %08X\n", ((u32*)eHead->e_ident)[0]);
		return false;
	}
	if (!(
		(eHead->e_ident[EI_CLASS]   == ELFCLASS64) &&
		(eHead->e_ident[EI_DATA]    == ELFDATA2LSB) &&
		(eHead->e_ident[EI_VERSION] == EV_CURRENT) &&
		(eHead->e_ident[EI_OSABI]   == ELFOSABI_FREEBSD)))
		return false;

	if ((eHead->e_type>>8) != 0xFE) // != ET_SCE_EXEC)
		printf(" Elf64::e_type: 0x%04X \n", eHead->e_type);

	if(!( (eHead->e_machine == EM_X86_64) && (eHead->e_version == EV_CURRENT) ))
		return false;

	return true;	
}
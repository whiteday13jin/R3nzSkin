#include <Windows.h>
#include <ctime>
#include <cstdlib>
#include <thread>
#include <regex>
#include <msclr/marshal_cppstd.h>

#include "R3nzUI.hpp"
#include "Injector.hpp"

using namespace System;
using namespace System::Windows::Forms;
using namespace System::Threading;
using namespace System::Globalization;
using namespace System::Net;

int main([[maybe_unused]] array<String^>^ args)
{
	std::srand(static_cast<unsigned int>(std::time(nullptr)));
	// 使用发布包目录定位配套 DLL，避免从快捷方式的工作目录加载错误文件。
	System::IO::Directory::SetCurrentDirectory(AppDomain::CurrentDomain->BaseDirectory);
	// Fork 通过带版本号的完整发布包更新，停用指向已归档仓库的旧自动更新入口。
	
	Application::EnableVisualStyles();
	Application::SetCompatibleTextRenderingDefault(false);
	R3nzSkinInjector::R3nzUI form;

	auto thread{ std::thread(Injector::run) };
	auto screenThread{ gcnew Thread(gcnew ThreadStart(%form, &R3nzSkinInjector::R3nzUI::updateScreen)) };
	screenThread->Start();

	Application::Run(%form);

	thread.detach();
	screenThread->Abort();

	return EXIT_SUCCESS;
}

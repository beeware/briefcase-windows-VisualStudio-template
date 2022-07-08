#pragma once

namespace Briefcase {

	using namespace System;
	using namespace System::ComponentModel;
	using namespace System::Collections;
	using namespace System::Windows::Forms;
	using namespace System::Data;
	using namespace System::Drawing;

	/// <summary>
	/// Summary for CrashDialog
	/// </summary>
	public ref class CrashDialog : public System::Windows::Forms::Form
	{
	public:
		CrashDialog(System::String^ details)
		{
			this->MinimizeBox = false;
			this->FormBorderStyle = System::Windows::Forms::FormBorderStyle::FixedSingle;
			this->MaximizeBox = false;
			this->Width = 540;
			this->Height = 320;
			this->Text = "Application has crashed";
			this->Icon = this->Icon->ExtractAssociatedIcon(Application::ExecutablePath);

			// The top-of-page introductory message
			System::Windows::Forms::Label^ label = gcnew System::Windows::Forms::Label();
			label->Left = 10;
			label->Top = 10;
			label->Width = 520;
			//label->Alignment = ContentAlignment::MiddleCenter;
			label->Text = "An unexpected error occurred. Please see the traceback below for more information.";

			this->Controls->Add(label);

			// A scrolling text box for the stack trace.
			System::Windows::Forms::RichTextBox^ trace = gcnew System::Windows::Forms::RichTextBox();
			trace->Left = 10;
			trace->Top = 30;
			trace->Width = 504;
			trace->Height = 210;
			trace->Multiline = true;
			trace->ReadOnly = true;
			trace->Font = gcnew System::Drawing::Font(
				System::Drawing::FontFamily::GenericMonospace,
				System::Drawing::SystemFonts::DefaultFont->Size,
				System::Drawing::FontStyle::Regular
			);
			trace->Text = details;

			this->Controls->Add(trace);

			System::Windows::Forms::Button^ accept = gcnew System::Windows::Forms::Button();
			accept->Left = 400;
			accept->Top = 250;
			accept->Width = 100;
			accept->Text = "Ok";
			accept->Click += gcnew System::EventHandler(this, &CrashDialog::CloseDialog);

			this->Controls->Add(accept);
		}

	protected:
		/// <summary>
		/// Clean up any resources being used.
		/// </summary>
		~CrashDialog()
		{
		}

	private:

		System::Void CloseDialog(System::Object^ sender, System::EventArgs^ e) {
			this->Close();
		}
	};
}

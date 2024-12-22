#pragma once


class NSSaturnTool
{
public:
	~NSSaturnTool();

	void InitializeApp();

	void UpdateApp();
	void UpdateUI();
	bool LoadMemoryFile(const std::string& InFileName);
};

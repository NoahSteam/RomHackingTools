#pragma once
void ExportToTextFiles(std::string csvFilePath, std::string outputDir) 
{
	// Open the CSV file
	std::ifstream csvFile(csvFilePath);

	// Read the column headers
	std::string line;
	std::getline(csvFile, line);
	std::istringstream iss(line);
	std::vector<std::string> headers;
	std::string header;
	while (std::getline(iss, header, ',')) 
	{
		headers.push_back(header);
	}

	// Initialize an unordered set to hold the filenames
	std::unordered_set<std::string> filenames;

	// Loop through the remaining lines in the CSV file
	while (std::getline(csvFile, line))
	{
		std::istringstream iss(line);
		std::vector<std::string> data;
		std::string value;
		while (std::getline(iss, value, ','))
		{
			data.push_back(value);
		}

		// Extract the filename from the first column
		std::string filename = data[0];

		// If the filename hasn't been processed yet, add it to the set
		if (filenames.find(filename) == filenames.end()) 
		{
			filenames.insert(filename);
		}
	}

	// Loop through the filenames and generate a text file for each one
	for (const auto& filename : filenames) 
	{
		// Open a new text file for writing in the output directory
		std::ofstream textFile(outputDir + "/" + filename + ".txt");

		// Loop through the lines in the CSV file and write the text to the text file
		csvFile.clear();
		csvFile.seekg(0);
		std::getline(csvFile, line);  // skip headers
		while (std::getline(csvFile, line)) 
		{
			std::istringstream iss(line);
			std::vector<std::string> data;
			std::string value;
			while (std::getline(iss, value, ',')) 
			{
				data.push_back(value);
			}

			// Write the line to the text file if the filename matches
			if (data[0] == filename) {
				textFile << data[2] << std::endl;
			}
		}

		// Close the text file
		textFile.close();
	}

	// Close the CSV file
	csvFile.close();
}

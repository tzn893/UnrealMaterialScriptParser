#pragma once

#include <vector>
#include <string>



namespace ums
{
	struct UMSParsedResult
	{
        struct FunctionParameters
        {
            enum ExtraAttribute
            {
				ATTRI_IN, ATTRI_OUT, ATTRI_INOUT
            } attribute;
		
			std::string type;
            std::string value;
		};

		struct Function
        {
            std::string ReturnType;
            std::string FunctionName;

			std::vector<FunctionParameters> Params;

			std::string FunctionBody;
		};

		struct Property
        {
            enum PropertyType
            {
				Float, Vector, Texture
            } Type;

			std::string Name;
            std::string DisplayName;
            std::string Value;
		};

		struct ShaderAttribute
        {
            std::string Key;
            std::string Value;
        };
        
		std::vector<Function> Functions;
        std::vector<std::string> IncludedFiles;
        std::vector<Property>    Properties;
        std::vector<ShaderAttribute> Attributes;

        std::string DebugDump();
	};

	class UMSParser
    {
    public:
        enum UMSParserResultCode
        {
            _Success, _LexerError, _ParseError, _UnexpectedError
        };

        UMSParserResultCode Parse(const char* FilePath, std::string& errorMsg, UMSParsedResult& Result);

        // Default input content encoding is UTF-8
        UMSParserResultCode ParseFromMemory(void* Content, size_t content_size, std::string& errorMsg, UMSParsedResult& Result);
	};

} // namespace ums

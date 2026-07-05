#include <cstdio>
#include <optional>
#include <string>
#include <vector>

#include <lexy/action/parse.hpp> // lexy::parse
#include <lexy/callback.hpp>     // value callbacks
#include <lexy/dsl.hpp>          // lexy::dsl::*
#include <lexy/input/file.hpp>   // lexy::read_file
#include <lexy/parse_tree.hpp>
#include <lexy/action/parse_as_tree.hpp>

#include <lexy_ext/report_error.hpp> // lexy_ext::report_error
#include <windows.h>  // 需要加这个头文件
#include <iostream>
#include "UMSParser.h"

#ifndef UMS_ASSERT
#define UMS_ASSERT(cond) if(!(cond)) {__debugbreak();}
#endif

namespace ums
{
    namespace dsl = lexy::dsl;

    constexpr const char* ums_code_keyword = "_Code";
    constexpr const char* ums_properties_keyword = "_Properties";

    struct vector4
    {
        float x, y, z, w;
    };

    std::ostream& operator<<(std::ostream& os, const vector4& v)
    {
        os << "(" << v.x << ", " << v.y << ", " << v.z << ", " << v.w << ")";
        return os;
    }

    struct ums_identifier
    {
        static constexpr auto rule
            = lexyd::identifier(lexyd::ascii::alpha_underscore, lexyd::ascii::alpha_digit_underscore);

        static constexpr auto value = lexy::as_string<std::string>;
    };

    constexpr auto ums_white_space = dsl::whitespace(dsl::ascii::space);

    struct ums_float
    {
        static constexpr auto rule = []() {
            auto sign   = dsl::opt(dsl::capture(LEXY_LIT("-")));
            auto upper  = dsl::capture(dsl::digits<>);
            auto period = dsl::capture(dsl::period);
            auto lower  = dsl::capture(dsl::digits<>);

            return sign + upper + period + lower + dsl::opt(LEXY_LIT("f"));
        }();
    };

    struct ums_vector4
    {
        static constexpr auto rule = dsl::parenthesized( dsl::times<4>(ums_white_space  + dsl::p<ums_float> + ums_white_space,
                          dsl::sep(dsl::comma)));
    };

    struct ums_comment_line
    {
        static constexpr auto rule = LEXY_LIT("//") + dsl::until(dsl::newline);
    };

    struct ums_comment_block
    {
        static constexpr auto rule = LEXY_LIT("/*") + dsl::until(LEXY_LIT("*/"));
    };

    struct ums_literal_value
    {
        static constexpr auto rule
            = dsl::peek(LEXY_LIT("\""))
                  >> (LEXY_LIT("\"") + dsl::p<ums_identifier> + LEXY_LIT("\""))
              | dsl::peek(LEXY_LIT("(")) >> dsl::p<ums_vector4> | dsl::else_ >> dsl::p<ums_float>;
    };

    struct ums_properties_definition
    {
        static constexpr auto rule = dsl::p<ums_identifier> + ums_white_space + LEXY_LIT("[")
                                     + ums_white_space + LEXY_LIT("\"") + dsl::p<ums_identifier>
                                     + ums_white_space + LEXY_LIT("\"") + ums_white_space + LEXY_LIT("]") + ums_white_space
                                        + LEXY_LIT("=") + ums_white_space
                                     + dsl::p<ums_literal_value> + ums_white_space  + LEXY_LIT(";");
    };

    // 语法类似 !ShaderMode=Forward
    struct ums_attributes_definition
    {
        static constexpr auto rule = LEXY_LIT("!") + ums_white_space + dsl::p<ums_identifier> + ums_white_space + LEXY_LIT("=") 
            + ums_white_space + dsl::p<ums_identifier> + ums_white_space  + LEXY_LIT(";");
    };

    struct ums_properties_body
    {
        static constexpr auto rule = dsl::while_(
            dsl::peek(LEXY_LIT("_")) >> dsl::p<ums_properties_definition> + ums_white_space
            | dsl::peek(LEXY_LIT("!")) >> dsl::p<ums_attributes_definition> + ums_white_space
            | dsl::peek(LEXY_LIT("//")) >> dsl::p<ums_comment_line> + ums_white_space);
    };

    struct ums_properties
    {
        static constexpr auto rule = LEXY_LIT("_Properties") + ums_white_space
                                     + LEXY_LIT("{") + ums_white_space + dsl::p<ums_properties_body>
                                     + ums_white_space + LEXY_LIT("}");
    };

    struct ums_function_parameter
    {
        static constexpr auto rule = dsl::opt(LEXY_LIT("inout")  | LEXY_LIT("in") | LEXY_LIT("out")) // hlsl有 inout, in, out 三种额外参数
                                     + ums_white_space 
            + dsl::p<ums_identifier> + ums_white_space + dsl::p<ums_identifier>;
    };

    struct ums_function_signature
    {
        static constexpr auto rule = dsl::p<ums_identifier> + ums_white_space   // 函数返回值
                                     + dsl::p<ums_identifier> + ums_white_space // 函数名称
                                     + LEXY_LIT("(") + ums_white_space
                                     + (dsl::peek(lexyd::ascii::alpha_underscore) >> dsl::p<ums_function_parameter> | dsl::else_ >> dsl::nullopt)  + ums_white_space// 第一个参数
                                     + dsl::while_(dsl::peek(LEXY_LIT(",")) >> (LEXY_LIT(",") + ums_white_space + dsl::p<ums_function_parameter> + ums_white_space)) // 如果有,、接着解析后续参数，直到所有参数被解析完
                                     + LEXY_LIT(")");
    };

    struct ums_scoped_body
    {
        static constexpr auto rule = LEXY_LIT("{") + dsl::while_(
            dsl::peek_not(LEXY_LIT("}")) >> (
                dsl::peek(LEXY_LIT("{")) >> dsl::recurse<ums_scoped_body>
                | dsl::else_ >> dsl::code_point
            )
        ) + LEXY_LIT("}");
    };


    struct ums_function
    {
        static constexpr auto rule = dsl::p<ums_function_signature> + ums_white_space
            + dsl::p<ums_scoped_body>;
    };

    struct ums_include_line
    {
        static constexpr auto rule = LEXY_LIT("#include") + ums_white_space + LEXY_LIT("\"") + dsl::until(LEXY_LIT("\""));
    };

    struct ums_code
    {
        static constexpr auto rule = LEXY_LIT("_Code") + ums_white_space + LEXY_LIT("{")
        + ums_white_space
        + dsl::while_(dsl::peek(dsl::ascii::space) >> ums_white_space 
            | dsl::peek(LEXY_LIT("//")) >> dsl::p<ums_comment_line> + ums_white_space
            | dsl::peek(LEXY_LIT("/*")) >> dsl::p<ums_comment_block> + ums_white_space
            | dsl::peek(LEXY_LIT("#")) >> dsl::p<ums_include_line> + ums_white_space
            | dsl::peek_not(LEXY_LIT("}")) >> dsl::p<ums_function>
        )
        + LEXY_LIT("}");
    };


    struct ums_program
    {
        // ums_code 和 ums_properties 缺一不可，顺序任意，中间可间隔任意空格。
        static constexpr auto rule = dsl::while_(
            dsl::peek(dsl::ascii::space) >> ums_white_space
            | dsl::peek(LEXY_LIT("_Properties")) >> dsl::p<ums_properties> + ums_white_space
            | dsl::peek(LEXY_LIT("_Code")) >> dsl::p<ums_code> + ums_white_space
        );
    };
} // namespace ums

template<typename gramma>
void parse_grammar(const char* file_path)
{
    auto file = lexy::read_file<lexy::utf8_encoding>(file_path);

    lexy::parse_tree_for<decltype(file.buffer())> tree;
    auto result = lexy::parse_as_tree<gramma>(tree, file.buffer(), lexy_ext::report_error);

    lexy::visualize(stdout, tree, {lexy::visualize_fancy});
}

static void print_escaped(const std::string& s)
{
        for (unsigned char c : s)
        {
            switch (c)
            {
            case '\n': std::cout << "\\n"; break;
            case '\t': std::cout << "\\t"; break;
            case '\r': std::cout << "\\r"; break;
            case '\b': std::cout << "\\b"; break;
            case '\f': std::cout << "\\f"; break;
            case '\v': std::cout << "\\v"; break;
            case '\\': std::cout << "\\\\"; break;
            case '\"': std::cout << "\\\""; break;
            default:
                std::cout << c;
            }
        }
    }


// ===================== Tree Consumer =====================
// 将 DoParse 内部的 lambda 提取为静态成员函数的上下文结构体

template<typename TreeRange>
struct UMSParseContext
{
    using node_iter = typename decltype(std::declval<TreeRange&>().begin());
    using event_t   = typename TreeRange::event;
    using node_type = typename std::decay_t<decltype(std::declval<node_iter>().deref().node)>;

    TreeRange&              tree_range;
    ums::UMSParsedResult&   Result;


    auto node_pop(node_iter& iter) const
    {
        
#ifdef _DEBUG
        auto [event, node] = iter.deref();
        iter.increment();
        
        std::string event_str;
        switch (event)
        {
            case event_t::enter: event_str = "enter"; break;
            case event_t::exit: event_str = "exit"; break;
            case event_t::leaf: event_str = "leaf"; break;
        }

        std::string node_name = node.kind().name();
        std::string node_content = std::string(node.lexeme().begin(), node.lexeme().end());
        std::cout << "[deref]: " << event_str << " [name]:" << node_name << " [content]:";
        print_escaped(node_content);
        std::cout << std::endl;

        return std::make_tuple(event, node);

#else
        auto rv = iter.deref();
        iter.increment();
        return rv;
#endif
    }

    static bool check_node_type(const node_type& node, const char* type_name)
    {
        return strcmp(node.kind().name(), type_name) == 0;
    }

    void skip_until(node_iter& iter, const char* target) const
    {
        while (iter != tree_range.end())
        {
            auto [_, node] = node_pop(iter);
            if (check_node_type(node, target))
            {
                break;
            }
        }
    }

    void exhaust_all_node_until_exit(node_iter& iter, const char* target) const
    {
        // 消耗后方的white_space直到 exit
        while (iter != tree_range.end())
        {
            auto [event, node] = node_pop(iter);

            if (event == event_t::exit && check_node_type(node, target))
                break;
        }
    }

    void consume_identifier(node_iter& iter, std::string& out_name) const
    {
        {
            auto [event, node] = node_pop(iter);
            UMS_ASSERT(check_node_type(node, "identifier"));
            auto lex = node.lexeme();
            out_name = std::string(lex.begin(), lex.end());
        }

        // 消耗一个跟随在后面的exit节点
        {
            auto [event, node] = node_pop(iter);
            UMS_ASSERT(event == event_t::exit);
        }
    }

    void consume_literal_value(node_iter& iter, std::string& out_content,
                                ums::UMSParsedResult::Property::PropertyType& Type) const
    {
        out_content.clear();
        iter.increment();

        int counter = 1;
        while (iter != tree_range.end())
        {
            auto [event, node] = node_pop(iter);

            auto lex = node.lexeme();
            out_content.append(lex.begin(), lex.end());

            if (event == event_t::exit)
            {
                counter--;
            }
            else if (event == event_t::enter)
            {
                counter++;
            }

            if (counter <= 0)
            {
                break;
            }
        }

        UMS_ASSERT(!out_content.empty());

        switch (out_content[0])
        {
        case '\"':
            Type = ums::UMSParsedResult::Property::PropertyType::Texture;
            break;
        case '(':
            Type = ums::UMSParsedResult::Property::PropertyType::Vector;
            break;
        default:
            Type = ums::UMSParsedResult::Property::PropertyType::Float;
            break;
        }
    }

    void consume_ums_properties_definition(node_iter& iter) const
    {
        ums::UMSParsedResult::Property property;

        // 第一个node肯定是ums_identifier
        {
            skip_until(iter, "ums_identifier");
            consume_identifier(iter, property.Name);
        }

        // 找到第二个ums_identifier 作为display name
        {
            skip_until(iter, "ums_identifier");
            consume_identifier(iter, property.DisplayName);
        }
        
        // 解析出最后的literal value
        {
            skip_until(iter, "ums_literal_value");
            consume_literal_value(iter, property.Value, property.Type);
        }

        // 消耗后方的white_space直到 exit
        exhaust_all_node_until_exit(iter, "ums_properties_definition");

        Result.Properties.push_back(property);
    }

    void consume_ums_comment_line(node_iter& iter) const
    {
        // 消耗掉 '//'
        node_pop(iter);
        node_pop(iter);
        node_pop(iter);
    }

    void consume_ums_attributes_definition(node_iter& iter) const
    {
        ums::UMSParsedResult::ShaderAttribute attribute;

        // 第一个 literal 是 !
        {
            auto [_, node] = node_pop(iter);
            UMS_ASSERT(check_node_type(node, "literal"));
        }

        // 第二个肯定是identifier
        {
            auto [_, node] = node_pop(iter);
            UMS_ASSERT(check_node_type(node, "ums_identifier"));

            consume_identifier(iter, attribute.Key);
        }

        // 找到下一个identifier
        {
            skip_until(iter, "ums_identifier");
            consume_identifier(iter, attribute.Value);
        }

        // 消耗后方的white_space直到 exit
        while (iter != tree_range.end())
        {
            auto [event, _] = node_pop(iter);

            if (event == event_t::exit)
                break;
        }

        Result.Attributes.push_back(attribute);
    }

    void parse_ums_properties(node_iter& iter) const
    {
        skip_until(iter, "ums_properties_body");

        while (iter != tree_range.end())
        {
            auto [event, node] = node_pop(iter);

            if (check_node_type(node, "ums_properties_definition"))
            {
                consume_ums_properties_definition(iter);
            }
            else if (check_node_type(node, "ums_attributes_definition"))
            {
                consume_ums_attributes_definition(iter);
            }
            else if (check_node_type(node, "ums_comment_line"))
            {
                consume_ums_comment_line(iter);
            }
            else if (event == event_t::exit && check_node_type(node, "ums_properties"))
            {
                break;
            }
        }
    }

    std::string consume_scoped_body(node_iter& iter) const
    {
        std::string body_content;

        // 跳过开头的 "{"
        {
            auto [event, node] = node_pop(iter);
            UMS_ASSERT(check_node_type(node, "literal")); // {

            body_content.append("{");
        }

        // 收集中间所有内容：code_point 字符 或 嵌套 ums_scoped_body
        while (iter != tree_range.end())
        {
            auto [event, node] = node_pop(iter);

            if (event == event_t::exit)
                break;

            if (check_node_type(node, "ums_scoped_body"))
            {
                // 递归收集嵌套 scope（不再需要 lambda self 传递，直接调用自身）
                std::string nested = consume_scoped_body(iter);
                body_content += nested;
            }
            else
            {
                auto lex = node.lexeme();
                body_content.append(lex.begin(), lex.end());
            }
        }

        return body_content;
    }

    ums::UMSParsedResult::FunctionParameters consume_function_parameter(node_iter& iter) const
    {
        ums::UMSParsedResult::FunctionParameters param;
        param.attribute = ums::UMSParsedResult::FunctionParameters::ATTRI_IN;

        // 第一个子节点可能是 literal (inout/in/out) 或直接是 ums_identifier
        {
            auto [event, node] = node_pop(iter);

            if (check_node_type(node, "literal"))
            {
                auto lex = node.lexeme();
                std::string attr(lex.begin(), lex.end());

                if (attr == "inout")
                    param.attribute = ums::UMSParsedResult::FunctionParameters::ATTRI_INOUT;
                else if (attr == "out")
                    param.attribute = ums::UMSParsedResult::FunctionParameters::ATTRI_OUT;
                else
                    param.attribute = ums::UMSParsedResult::FunctionParameters::ATTRI_IN;
            }

            // 找到第一个节点对应的identifier
            if(!check_node_type(node, "ums_identifier"))
            {
                skip_until(iter, "ums_identifier");
            }
            consume_identifier(iter, param.type);
        }

        // 第二个 identifier 是参数名
        {
            skip_until(iter, "ums_identifier");
            consume_identifier(iter, param.value);
        }

        exhaust_all_node_until_exit(iter, "ums_function_parameter");

        return param;
    }

    ums::UMSParsedResult::Function consume_function_signature(node_iter& iter) const
    {
        ums::UMSParsedResult::Function func;

        // 第一个 identifier 是返回类型
        {
            auto [_, node] = node_pop(iter);
            UMS_ASSERT(check_node_type(node, "ums_identifier"));

            consume_identifier(iter, func.ReturnType);
        }

        // 第二个 identifier 是函数名
        {
            skip_until(iter, "ums_identifier");
            consume_identifier(iter, func.FunctionName);
        }

        // 跳过 "(" literal
        {
            skip_until(iter, "literal");
        }

        // 解析可选参数列表
        while (iter != tree_range.end())
        {
            auto [event, node] = node_pop(iter);

            if (check_node_type(node, "ums_function_parameter"))
            {
                func.Params.push_back(consume_function_parameter(iter));
            }
            else if (event == event_t::exit && check_node_type(node, "ums_function_signature"))
            {
                break;
            }
        }

        return func;
    }

    ums::UMSParsedResult::Function consume_function(node_iter& iter) const
    {
        auto [_, node] = node_pop(iter);
        UMS_ASSERT(check_node_type(node, "ums_function_signature"));

        auto func = consume_function_signature(iter);

        skip_until(iter, "ums_scoped_body");
        func.FunctionBody = consume_scoped_body(iter);

        exhaust_all_node_until_exit(iter, "ums_function");
        return func;
    }

    void consume_parse_include(node_iter& iter) const
    {
        std::string include_path;

        // 跳过 "#include" literal
        {
            auto [_, inc_node] = node_pop(iter);
        }

        // 跳过 """ literal
        {
            auto [_, quote_node] = node_pop(iter);
        }

        // 收集路径内容直到 exit
        while (iter != tree_range.end())
        {
            auto [ev, n] = node_pop(iter);

            if (ev == event_t::exit)
                break;

            auto lex = n.lexeme();
            include_path.append(lex.begin(), lex.end());
        }

        if (*include_path.rbegin() == '\"')
        {
            include_path.pop_back();
        };
        if (*include_path.begin() == '\"')
        {
            include_path.erase(include_path.begin());
        }
        Result.IncludedFiles.push_back(include_path);
    }

    void parse_ums_codes(node_iter& iter) const
    {
        while (iter != tree_range.end())
        {
            auto [event, node] = node_pop(iter);

            if (event == event_t::exit && check_node_type(node, "ums_code"))
                break;

            if (check_node_type(node, "ums_comment_line"))
            {
                consume_ums_comment_line(iter);
            }
            else if (check_node_type(node, "ums_comment_block"))
            {
                while (iter != tree_range.end())
                {
                    auto [ev, n] = node_pop(iter);
                    if (ev == event_t::exit)
                        break;
                }
            }
            else if (check_node_type(node, "ums_include_line"))
            {
                consume_parse_include(iter);
            }
            else if (check_node_type(node, "ums_function"))
            {
                Result.Functions.push_back(consume_function(iter));
            }
        }
    }
};

// ===================== DoParse 入口 =====================

template<typename BufferType>
static ums::UMSParser::UMSParserResultCode DoParse(BufferType& buffer, std::string& errorMsg, ums::UMSParsedResult& Result)
{
    lexy::parse_tree_for<decltype(buffer)> tree;

    std::string error_output;
    auto callback = lexy_ext::report_error.to(std::back_inserter(error_output));

    auto result = lexy::parse_as_tree<ums::ums_program>(tree, buffer, callback);

    if (!result.is_success())
    {
        errorMsg = error_output;
        return ums::UMSParser::_LexerError;
    }

    auto tree_range      = tree.traverse();
    auto tree_node_iter  = tree_range.begin();

    using TreeRange = decltype(tree_range);
    UMSParseContext<TreeRange> ctx{tree_range, Result};

    bool b_properties_parsed = false;
    bool b_code_parsed       = false;

    while (tree_node_iter != tree_range.end())
    {
        auto [event, node] = ctx.node_pop(tree_node_iter);

        std::string node_name = node.kind().name();
        if (node_name == "ums_properties")
        {
            b_properties_parsed = true;
            ctx.parse_ums_properties(tree_node_iter);
        }
        else if (node_name == "ums_code")
        {
            b_code_parsed = true;
            ctx.parse_ums_codes(tree_node_iter);
        }
    }

    if (!b_properties_parsed)
    {
        errorMsg = "_Properties Block is missing";
        return ums::UMSParser::_ParseError;
    }

    if (!b_code_parsed)
    {
        errorMsg = "_Code Block is missing";
        return ums::UMSParser::_ParseError;
    }

    return ums::UMSParser::_Success;
}

// ===================== 工具函数 =====================

static std::string GetFileErrorString(lexy::file_error Error)
{
    switch (Error)
    {
    case lexy::file_error::file_not_found:
        return " file not found";
    case lexy::file_error::os_error:
        return " os error";
    case lexy::file_error::permission_denied:
        return " permission_denied";
    }
    return " unknown error";
}

ums::UMSParser::UMSParserResultCode ums::UMSParser::Parse(const char* FilePath, std::string& errorMsg, ums::UMSParsedResult& Result)
{
    if (!FilePath)
    {
        errorMsg = "FilePath is nullptr";
        return ums::UMSParser::_UnexpectedError;
    }

    auto file = lexy::read_file<lexy::utf8_encoding>(FilePath);
    if (!file)
    {
        std::string file_error = GetFileErrorString(file.error());
        errorMsg = " file error encountered! :" + file_error + " for file " + std::string(FilePath);
        return ums::UMSParser::_UnexpectedError;
    }

    return DoParse(file.buffer(), errorMsg, Result);
}

ums::UMSParser::UMSParserResultCode ums::UMSParser::ParseFromMemory(void* Content,size_t content_size, std::string& errorMsg, ums::UMSParsedResult& Result)
{
    if (!Content)
    {
        errorMsg = "Content is nullptr";
        return ums::UMSParser::_UnexpectedError;
    }

    auto buffer_builder = lexy::buffer<lexy::utf8_encoding>::builder(content_size);
    memcpy(buffer_builder.data(), Content, content_size);

    auto buffer = std::move(buffer_builder).finish();
    return DoParse(buffer, errorMsg, Result);
}


std::string ums::UMSParsedResult::DebugDump()
{
    std::string output;
    auto append_line = [&](const std::string& line) { output += line + "\n"; };

    append_line("========== UMSParsedResult DebugDump ==========");

    // ---- Properties ----
    append_line("[Properties] ( Count: " + std::to_string(Properties.size()) + ")");
    for (size_t i = 0; i < Properties.size(); ++i)
    {
        const auto& p = Properties[i];
        const char* type_str = (p.Type == Property::Float)   ? "Float"
                               : (p.Type == Property::Vector) ? "Vector"
                                                               : "Texture";
        append_line("  [" + std::to_string(i) + "] " + p.Name
                     + " [\"" + p.DisplayName + "\"]"
                     + " (" + type_str + ")"
                     + " = " + p.Value);
    }
    append_line("");

    // ---- Attributes ----
    append_line("[Attributes] ( Count: " + std::to_string(Attributes.size()) + ")");
    for (size_t i = 0; i < Attributes.size(); ++i)
    {
        const auto& a = Attributes[i];
        append_line("  [" + std::to_string(i) + "] !" + a.Key + " = " + a.Value);
    }
    append_line("");

    // ---- Included Files ----
    append_line("[IncludedFiles] ( Count: " + std::to_string(IncludedFiles.size()) + ")");
    for (size_t i = 0; i < IncludedFiles.size(); ++i)
    {
        append_line("  [" + std::to_string(i) + "] #include \"" + IncludedFiles[i] + "\"");
    }
    append_line("");

    // ---- Functions ----
    append_line("[Functions] ( Count: " + std::to_string(Functions.size()) + ")");
    for (size_t fi = 0; fi < Functions.size(); ++fi)
    {
        const auto& f = Functions[fi];

        // 签名行
        std::string sig = f.ReturnType + " " + f.FunctionName + "(";
        for (size_t pi = 0; pi < f.Params.size(); ++pi)
        {
            if (pi > 0)
                sig += ", ";
            const auto& param = f.Params[pi];
            const char* attr_str = (param.attribute == FunctionParameters::ATTRI_IN)    ? ""
                                   : (param.attribute == FunctionParameters::ATTRI_OUT) ? "out "
                                                                                       : "inout ";
            sig += attr_str + param.type + " " + param.value;
        }
        sig += ")";

        append_line("  Function[" + std::to_string(fi) + "]: " + sig);

        // 函数体（缩进显示）
        if (!f.FunctionBody.empty())
        {
            append_line("    Body:");
            std::string body = f.FunctionBody;

            // 按行分割并缩进
            size_t pos = 0;
            while (pos < body.size())
            {
                size_t end = body.find('\n', pos);
                if (end == std::string::npos)
                    end = body.size();

                std::string line = body.substr(pos, end - pos);

                // 去除行首空白再统一加4格缩进，避免双重缩进
                size_t leading = line.find_first_not_of(" \t\r");
                if (leading != std::string::npos && leading > 0)
                    line = line.substr(leading);

                if (!line.empty() || end != body.size()) // 不追加末尾空行
                    append_line("      | " + line);

                pos = end + 1;
            }
        }
        else
        {
            append_line("    Body: (empty)");
        }

        append_line("");
    }

    append_line("==============================================");
    return output;
}


#ifdef UMS_DO_TEST

#include <fstream>
#include <ctime>

// 单个目录的测试结果统计
struct UMSDirTestStats
{
    int total = 0; // 文件总数
    int pass  = 0; // 解析结果符合预期
    int fail  = 0; // 解析结果不符合预期（需排查）
};

// 遍历指定目录下所有 .ums 文件，逐个执行 Parse，并把结果写入 out。
// expect_success=true  表示正例：预期解析成功（失败则为 UNEXPECTED FAIL）
// expect_success=false 表示反例：预期解析失败（成功则为 UNEXPECTED SUCCESS）
static UMSDirTestStats UMSRunTestDirectory(std::ofstream& out,
                                            const std::string& dir,
                                            const char* label,
                                            bool expect_success)
{
    UMSDirTestStats stats;

    out << "########################################\n";
    out << "[" << label << "] Directory: " << dir << "\n";
    out << "Expectation: " << (expect_success ? "PARSE SUCCESS" : "PARSE FAILURE") << "\n";
    out << "########################################\n\n";

    WIN32_FIND_DATAA ffd;
    HANDLE hFind = FindFirstFileA((dir + "*.ums").c_str(), &ffd);
    if (hFind == INVALID_HANDLE_VALUE)
    {
        out << "No .ums files found in directory.\n\n";
        return stats;
    }

    do
    {
        std::string filename = ffd.cFileName;
        if (filename == "." || filename == "..")
            continue;
        if (filename.size() < 4 || filename.compare(filename.size() - 4, 4, ".ums") != 0)
            continue;

        const std::string full_path = dir + filename;

        std::string        errorMsg;
        ums::UMSParsedResult Result;
        ums::UMSParser     Parser;

        auto result   = Parser.Parse(full_path.c_str(), errorMsg, Result);
        bool success  = (result == ums::UMSParser::_Success);

        ++stats.total;
        out << "----------------------------------------\n";
        out << "File: " << filename << "\n";

        if (success == expect_success)
        {
            ++stats.pass;
            if (success)
            {
                out << "[EXPECTED SUCCESS] ResultCode = " << static_cast<int>(result) << "\n";
            }
            else
            {
                out << "[EXPECTED FAIL] ResultCode = " << static_cast<int>(result) << "\n";
                out << "ErrorMsg:\n" << errorMsg << "\n";
            }
        }
        else
        {
            ++stats.fail;
            if (success)
            {
                // 反例却解析成功
                out << "[UNEXPECTED SUCCESS] This negative example parsed successfully!\n";
                out << "DebugDump:\n" << Result.DebugDump() << "\n";
            }
            else
            {
                // 正例却解析失败
                out << "[UNEXPECTED FAIL] This positive example failed to parse!\n";
                out << "ErrorMsg:\n" << errorMsg << "\n";
            }
        }
        out << "\n";
    } while (FindNextFileA(hFind, &ffd) != 0);
    FindClose(hFind);

    return stats;
}

// 测试入口：当定义了 UMS_DO_TEST 宏时启用。
// 同时跑 negative（反例，期望失败）与根目录正例（期望成功），结果写入文件。
int main()
{
    SetConsoleOutputCP(65001); // 设置控制台输出为 UTF-8
    SetConsoleCP(65001);       // 设置控制台输入为 UTF-8

    const std::string negative_dir = "E:\\work\\UnrealMaterialScriptParser\\ums_examples\\negative\\";
    const std::string positive_dir = "E:\\work\\UnrealMaterialScriptParser\\ums_examples\\";
    const std::string output_path  = "E:\\work\\UnrealMaterialScriptParser\\ums_examples\\test_result.txt";

    std::ofstream out(output_path, std::ios::binary);
    if (!out.is_open())
    {
        std::cerr << "Cannot open output file: " << output_path << std::endl;
        return 1;
    }

    // 写入 UTF-8 BOM，方便在 Windows 记事本等工具中正常显示中文
    out << '\xEF' << '\xBB' << '\xBF';

    // 生成时间戳
    std::time_t now = std::time(nullptr);
    char time_buf[64] = {0};
    std::strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", std::localtime(&now));

    out << "UMS Examples Test Result\n";
    out << "Generated : " << time_buf << "\n";
    out << "========================================\n\n";

    // 反例：期望解析失败
    auto neg = UMSRunTestDirectory(out, negative_dir, "NEGATIVE", /*expect_success=*/false);
    out << "\n";
    // 正例：期望解析成功（根目录下的 .ums 完整程序）
    auto pos = UMSRunTestDirectory(out, positive_dir, "POSITIVE", /*expect_success=*/true);

    int total_mismatch = neg.fail + pos.fail;

    out << "========================================\n";
    out << "Summary:\n";
    out << "  [NEGATIVE] total=" << neg.total << " pass=" << neg.pass << " mismatch=" << neg.fail << "\n";
    out << "  [POSITIVE] total=" << pos.total << " pass=" << pos.pass << " mismatch=" << pos.fail << "\n";
    out << "  Total mismatches : " << total_mismatch << "\n";
    out << "========================================\n";
    out.close();

    std::cout << "Test finished. mismatches=" << total_mismatch << "\n";
    std::cout << "Results written to: " << output_path << std::endl;

    // 全部文件都符合预期则视为测试通过（返回 0），否则返回 2
    return (total_mismatch == 0) ? 0 : 2;
}

#else // !UMS_DO_TEST

int main()
{
    SetConsoleOutputCP(65001); // 设置控制台输出为 UTF-8
    SetConsoleCP(65001);       // 设置控制台输入为 UTF-8

    std::string errorMsg;

    ums::UMSParser Parser;
    ums::UMSParsedResult Result;


    auto result = Parser.Parse("E:\\work\\UnrealMaterialScriptParser\\ums_examples\\negative\\unbalanced_brace.ums", errorMsg, Result);


    if (result != ums::UMSParser::_Success)
    {
        std::cout << "Parse Error: " << errorMsg << std::endl;
        return 1;
    }
    else
    {
        std::cout << Result.DebugDump() << std::endl;
        return 0;
    }
}

#endif // UMS_DO_TEST

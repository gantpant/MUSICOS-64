#include <cstdint>

enum class TokenType {
    IDENTIFIER,
    STRING,
    NUMBER,
    LEFT_PAREN,
    RIGHT_PAREN,
    END
};

struct Token {
    TokenType type;
    const char* value;    
};

class Lexer {
    const char* source;
    uint64_t position;
char number_buffer[32];
uint64_t buffer_position = 0;
public:
    char peek() {
        return source[position];
    }
    Lexer(const char* input) {
        source = input;
        position = 0;
    }
    void advance() {
        position++;
    }
    bool is_end() {
        if (peek() == '\0') {
            return true;
        }
        else {
            return false;
        }
    }
    void skip_whitespace() {
        while (peek() == ' ' || peek() == '\t') {
            advance();
        }
    }
    Token read_number() {
        uint64_t start = position;
        while (peek() >= '0' && peek() <= '9') {
            number_buffer[buffer_position] = peek();
            buffer_position += 1;
            advance();
        }
        number_buffer[buffer_position + 1] = '\0';
    }
};


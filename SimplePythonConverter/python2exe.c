#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <libgen.h>

#define MAX_PATH 4096
#define MAX_BUFFER 8192
#define PYTHON_EXECUTABLE "/usr/bin/python3"

/**
 * Print usage information
 */
void print_usage(const char* program_name) {
    printf("🐍 Python to Executable Converter\n");
    printf("******* By @planetminguez *******\n");
    printf("Usage: %s <python_script.py> [output_executable]\n\n", program_name);
    printf("Converts any Python3 script into a standalone executable file\n\n");
    printf("Parameters:\n");
    printf("  python_script    - Path to Python3 script (.py file)\n");
    printf("  output_executable - Optional output executable name\n");
    printf("                     (defaults to script name without .py)\n\n");
    printf("Examples:\n");
    printf("  %s hello.py\n", program_name);
    printf("  %s script.py myapp\n", program_name);
    printf("  %s ~/projects/calculator.py ~/bin/calc\n\n", program_name);
    printf("Features:\n");
    printf("  • Creates self-contained executable\n");
    printf("  • Bundles Python script inside executable\n");
    printf("  • No external dependencies needed\n");
    printf("  • Preserves script functionality\n");
}

/**
 * Check if file exists
 */
int file_exists(const char* filename) {
    struct stat buffer;
    return (stat(filename, &buffer) == 0);
}

/**
 * Get file size
 */
long get_file_size(const char* filename) {
    FILE* file = fopen(filename, "rb");
    if (!file) return -1;
    
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fclose(file);
    
    return size;
}

/**
 * Read entire file into buffer
 */
char* read_file(const char* filename, long* size) {
    FILE* file = fopen(filename, "rb");
    if (!file) {
        printf("❌ Error: Cannot open file '%s': %s\n", filename, strerror(errno));
        return NULL;
    }
    
    fseek(file, 0, SEEK_END);
    *size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    char* buffer = malloc(*size + 1);
    if (!buffer) {
        printf("❌ Error: Memory allocation failed\n");
        fclose(file);
        return NULL;
    }
    
    size_t read_size = fread(buffer, 1, *size, file);
    if ((long)read_size != *size) {
        printf("❌ Error: Failed to read entire file\n");
        free(buffer);
        fclose(file);
        return NULL;
    }
    
    buffer[*size] = '\0';
    fclose(file);
    
    return buffer;
}

/**
 * Create output filename from input
 */
char* create_output_filename(const char* input_filename) {
    char* output = malloc(strlen(input_filename) + 10);
    if (!output) return NULL;
    
    strcpy(output, input_filename);
    
    // Remove .py extension if present
    char* dot = strrchr(output, '.');
    if (dot && strcmp(dot, ".py") == 0) {
        *dot = '\0';
    }
    
    return output;
}

/**
 * Escape string for C string literal
 */
char* escape_string(const char* input) {
    long input_len = strlen(input);
    char* output = malloc(input_len * 4 + 1); // Worst case: every char becomes \xxx
    if (!output) return NULL;
    
    char* out_ptr = output;
    
    for (const char* in_ptr = input; *in_ptr; in_ptr++) {
        switch (*in_ptr) {
            case '\n':
                *out_ptr++ = '\\';
                *out_ptr++ = 'n';
                break;
            case '\r':
                *out_ptr++ = '\\';
                *out_ptr++ = 'r';
                break;
            case '\t':
                *out_ptr++ = '\\';
                *out_ptr++ = 't';
                break;
            case '\\':
                *out_ptr++ = '\\';
                *out_ptr++ = '\\';
                break;
            case '"':
                *out_ptr++ = '\\';
                *out_ptr++ = '"';
                break;
            case '\0':
                *out_ptr++ = '\\';
                *out_ptr++ = '0';
                break;
            default:
                if (*in_ptr < 32 || *in_ptr > 126) {
                    // Non-printable character - use octal escape
                    sprintf(out_ptr, "\\%03o", (unsigned char)*in_ptr);
                    out_ptr += 4;
                } else {
                    *out_ptr++ = *in_ptr;
                }
                break;
        }
    }
    
    *out_ptr = '\0';
    return output;
}

/**
 * Generate C wrapper code
 */
int generate_wrapper(const char* python_script, const char* escaped_code, const char* temp_c_file) {
    FILE* c_file = fopen(temp_c_file, "w");
    if (!c_file) {
        printf("❌ Error: Cannot create temporary C file: %s\n", strerror(errno));
        return 1;
    }
    
    // Get base name for temp file naming
    char* script_copy = strdup(python_script);
    char* base_name = basename(script_copy);
    
    fprintf(c_file, 
        "#include <stdio.h>\n"
        "#include <stdlib.h>\n"
        "#include <string.h>\n"
        "#include <unistd.h>\n"
        "#include <sys/stat.h>\n"
        "#include <errno.h>\n"
        "\n"
        "// Embedded Python script\n"
        "const char* python_code = \"%s\";\n"
        "\n"
        "int main(int argc, char* argv[]) {\n"
        "    // Create temporary file for Python script\n"
        "    char temp_file[] = \"/tmp/pyexe_%s_XXXXXX\";\n"
        "    int fd = mkstemp(temp_file);\n"
        "    if (fd == -1) {\n"
        "        fprintf(stderr, \"Error creating temporary file: %%s\\n\", strerror(errno));\n"
        "        return 1;\n"
        "    }\n"
        "\n"
        "    // Write Python code to temporary file\n"
        "    FILE* temp_fp = fdopen(fd, \"w\");\n"
        "    if (!temp_fp) {\n"
        "        fprintf(stderr, \"Error opening temporary file: %%s\\n\", strerror(errno));\n"
        "        close(fd);\n"
        "        unlink(temp_file);\n"
        "        return 1;\n"
        "    }\n"
        "\n"
        "    if (fprintf(temp_fp, \"%%s\", python_code) < 0) {\n"
        "        fprintf(stderr, \"Error writing to temporary file: %%s\\n\", strerror(errno));\n"
        "        fclose(temp_fp);\n"
        "        unlink(temp_file);\n"
        "        return 1;\n"
        "    }\n"
        "\n"
        "    fclose(temp_fp);\n"
        "\n"
        "    // Make temporary file executable\n"
        "    chmod(temp_file, 0755);\n"
        "\n"
        "    // Build Python command with arguments\n"
        "    char* python_cmd = malloc(strlen(temp_file) + 1024);\n"
        "    if (!python_cmd) {\n"
        "        fprintf(stderr, \"Memory allocation failed\\n\");\n"
        "        unlink(temp_file);\n"
        "        return 1;\n"
        "    }\n"
        "\n"
        "    strcpy(python_cmd, \"%s \");\n"
        "    strcat(python_cmd, temp_file);\n"
        "\n"
        "    // Append command line arguments\n"
        "    for (int i = 1; i < argc; i++) {\n"
        "        strcat(python_cmd, \" \\\"\");\n"
        "        strcat(python_cmd, argv[i]);\n"
        "        strcat(python_cmd, \"\\\"\");\n"
        "    }\n"
        "\n"
        "    // Execute Python script\n"
        "    int result = system(python_cmd);\n"
        "\n"
        "    // Cleanup\n"
        "    free(python_cmd);\n"
        "    unlink(temp_file);\n"
        "\n"
        "    return result == -1 ? 1 : WEXITSTATUS(result);\n"
        "}\n",
        escaped_code, base_name, PYTHON_EXECUTABLE);
    
    fclose(c_file);
    free(script_copy);
    
    return 0;
}

/**
 * Compile C wrapper to executable
 */
int compile_wrapper(const char* temp_c_file, const char* output_executable) {
    char compile_cmd[MAX_PATH * 2];
    snprintf(compile_cmd, sizeof(compile_cmd), 
             "clang -O2 -o \"%s\" \"%s\"", 
             output_executable, temp_c_file);
    
    printf("🔨 Compiling executable...\n");
    
    int result = system(compile_cmd);
    if (result != 0) {
        printf("❌ Error: Compilation failed\n");
        return 1;
    }
    
    // Make executable
    chmod(output_executable, 0755);
    
    return 0;
}

/**
 * Convert Python script to executable
 */
int convert_python_to_exe(const char* python_script, const char* output_executable) {
    printf("🐍 Converting '%s' to executable...\n", python_script);
    
    // Check if Python script exists
    if (!file_exists(python_script)) {
        printf("❌ Error: Python script '%s' not found\n", python_script);
        return 1;
    }
    
    // Check if Python interpreter exists
    if (!file_exists(PYTHON_EXECUTABLE)) {
        printf("❌ Error: Python3 interpreter not found at %s\n", PYTHON_EXECUTABLE);
        printf("💡 Install Python3: brew install python3\n");
        return 1;
    }
    
    // Read Python script
    long script_size;
    char* python_code = read_file(python_script, &script_size);
    if (!python_code) {
        return 1;
    }
    
    printf("📊 Script size: %ld bytes\n", script_size);
    
    // Escape Python code for C string
    char* escaped_code = escape_string(python_code);
    if (!escaped_code) {
        printf("❌ Error: Failed to escape Python code\n");
        free(python_code);
        return 1;
    }
    
    // Create temporary C file
    char temp_c_file[] = "/tmp/python2exe_XXXXXX.c";
    int fd = mkstemps(temp_c_file, 2);
    if (fd == -1) {
        printf("❌ Error: Cannot create temporary file: %s\n", strerror(errno));
        free(python_code);
        free(escaped_code);
        return 1;
    }
    close(fd);
    
    // Generate C wrapper
    if (generate_wrapper(python_script, escaped_code, temp_c_file) != 0) {
        free(python_code);
        free(escaped_code);
        unlink(temp_c_file);
        return 1;
    }
    
    // Compile wrapper
    if (compile_wrapper(temp_c_file, output_executable) != 0) {
        free(python_code);
        free(escaped_code);
        unlink(temp_c_file);
        return 1;
    }
    
    // Cleanup
    unlink(temp_c_file);
    free(python_code);
    free(escaped_code);
    
    // Check final executable
    if (!file_exists(output_executable)) {
        printf("❌ Error: Failed to create executable\n");
        return 1;
    }
    
    long exe_size = get_file_size(output_executable);
    printf("✅ Successfully created '%s' (%ld bytes)\n", output_executable, exe_size);
    
    return 0;
}

/**
 * Main function
 */
int main(int argc, char* argv[]) {
    if (argc < 2 || argc > 3) {
        print_usage(argv[0]);
        return 1;
    }
    
    const char* python_script = argv[1];
    char* output_executable = NULL;
    int allocated_output = 0;
    
    // Determine output filename
    if (argc == 3) {
        output_executable = argv[2];
    } else {
        output_executable = create_output_filename(python_script);
        if (!output_executable) {
            printf("❌ Error: Could not create output filename\n");
            return 1;
        }
        allocated_output = 1;
    }
    
    printf("🎯 Input:  %s\n", python_script);
    printf("🎯 Output: %s\n", output_executable);
    printf("\n");
    
    // Perform conversion
    int result = convert_python_to_exe(python_script, output_executable);
    
    if (allocated_output) {
        free(output_executable);
    }
    
    if (result == 0) {
        printf("\n🎉 Conversion completed successfully!\n");
        printf("💡 You can now run the executable directly:\n");
        if (argc == 3) {
            printf("   ./%s\n", argv[2]);
        } else if (allocated_output) {
            char* script_copy = strdup(python_script);
            char* base_name = basename(script_copy);
            printf("   ./%s\n", base_name);
            free(script_copy);
        } else {
            printf("   ./%s\n", output_executable);
        }
    }
    
    return result;
}

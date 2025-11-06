#### INSTALACIÓN
Antes de instalar el envoltorio, asegúrate de tener instalado `CMake` en tu sistema. Puedes descargarlo desde [aquí](https://cmake.org/download/).

Puedes instalarlo utilizando CMake con `FetchContent`. Agrega lo siguiente a tu archivo `CMakeLists.txt`:
```cmake
include(FetchContent)
FetchContent_Declare(
    ZeroBounce
    GIT_REPOSITORY https://github.com/zerobounce/zerobounce-c-api-wrapper.git
    GIT_TAG <DESIRED_TAG>
)
FetchContent_MakeAvailable(ZeroBounce)
```

Esto generará el objetivo `ZeroBounce` que puedes enlazar de la forma típica:

```cmake
target_link_libraries(nombre_de_tu_objetivo PRIVATE ZeroBounce)
```

Si estás utilizando Visual Studio Code, puedes construir la biblioteca utilizando las extensiones `CMake` y `Cmake Tools`. Después de instalar las extensiones, simplemente abre los comandos con `CTRL + SHIFT + P` y usa `CMake: Configure` seguido de `CMake: Build`.

Alternativamente, también puedes utilizar los comandos de la CLI en el directorio raíz de tu proyecto.

###### LINUX

```bash
mkdir build
cmake -S . -B build -G "Unix Makefiles"
cmake --build build
```

###### WINDOWS

Asegúrate de tener `MinGW` instalado en tu sistema. Puede instalarse siguiendo los pasos [aquí](https://code.visualstudio.com/docs/cpp/config-mingw).

```bash
mkdir build
cmake -S . -B build -G "MinGW Makefiles"
cmake --build build
```

Ten en cuenta que es posible que la biblioteca requiera algunos archivos .dll (que se encuentran en el directorio `build`) para funcionar correctamente en tu proyecto.

#### USO
Incluye la biblioteca en tu archivo:
```c
###include <ZeroBounce/ZeroBounce.h>
```

Inicializa el envoltorio con tu clave de API y tu API preferida:
```c
ZeroBounce* zb = zero_bounce_get_instance();
zero_bounce_initialize(zb, "<YOUR_API_KEY>", ZB_Api_URL_Default);
```

#### Ejemplos
Luego puedes usar cualquiera de las funciones de envoltura, por ejemplo:

####### Verifica cuántos créditos te quedan en tu cuenta
```c
void on_error(ZBErrorResponse error_response) {
    printf("%s\n\n", zb_error_response_to_string(&error_response));
}

void on_success_credits(ZBCreditsResponse response) {
    printf("%s\n\n", zb_credits_response_to_string(&response));
}

get_credits(zb, on_success_credits, on_error);
```

####### Verifica el uso de la API para un período de tiempo determinado
```c
void on_error(ZBErrorResponse error_response) {
    printf("%s\n\n", zb_error_response_to_string(&error_response));
}

void on_success_api_usage(ZBGetApiUsageResponse response) {
    printf("%s\n\n", zb_get_api_usage_response_to_string(&response));
}

struct tm start_date;
start_date.tm_year = 118;
start_date.tm_mon = 0;
start_date.tm_mday = 1;

struct tm end_date;
end_date.tm_year = 123;
end_date.tm_mon = 11;
end_date.tm_mday = 12;

get_api_usage(zb, start_date, end_date, on_success_api_usage, on_error);
```

####### Valida una dirección de correo electrónico
```c
void on_error(ZBErrorResponse error_response) {
    printf("%s\n\n", zb_error_response_to_string(&error_response));
}

void on_success_validate(ZBValidateResponse response) {
    printf("%s\n\n", zb_validate_response_to_string(&response));
}

char* email = "valid@example.com";   // La dirección de correo electrónico que deseas validar
char* ip_address = "127.0.0.1";     // La dirección IP desde la cual se registró el correo electrónico (opcional)

validate_email(zb, email, ip_address, on_success_validate, on_error);
```

####### Valida un lote de correos electrónicos
```c
void on_error(ZBErrorResponse error_response) {
    printf("%s\n\n", zb_error_response_to_string(&error_response));
}

void on_success_validate_batch(ZBValidateBatchResponse response) {
    printf("%s\n\n", zb_validate_batch_response_to_string(&response));
}

EmailToValidateVector vector = email_to_validate_vector_init();

ZBEmailToValidate email_1 = {"valid@example.com", "1.1.1.1"};
email_to_validate_vector_append(&vector, email_1);

ZBEmailToValidate email_2 = {"invalid@example.com", ""};
email_to_validate_vector_append(&vector, email_2);

validate_email_batch(zb, vector, on_success_validate_batch, on_error);

email_to_validate_vector_free(&vector);
```

####### Utiliza el punto de conexión de la API de actividad para obtener información sobre el compromiso general de los suscriptores con los correos electrónicos
```c
void on_error(ZBErrorResponse error_response) {
    printf("%s\n\n", zb_error_response_to_string(&error_response));
}

void on_success_activity_data(ZBActivityDataResponse response) {
    printf("%s\n\n", zb_activity_data_response_to_string(&response));
}

char* email = "valid@example.com";    // Dirección de correo electrónico del suscriptor

get_activity_data(zb, email, on_success_activity_data, on_error);
```

####### La API sendfile permite al usuario enviar un archivo para validar correos electrónicos a granel
```c
void on_error(ZBErrorResponse error_response) {
    printf("%s\n\n", zb_error_response_to_string(&error_response));


}

void on_success_send_file(ZBSendFileResponse response) {
    printf("%s\n\n", zb_send_file_response_to_string(&response));
}

char* file_path = "<RUTA_DEL_ARCHIVO>"; 			// Ruta del archivo CSV o TXT
int email_address_column = 3; 			// Índice de la columna "email" en el archivo. El índice comienza en 1
SendFileOptions options = new_send_file_options();	// Opciones adicionales

options.returnUrl = "https://dominio.com/llamado/despues/procesamiento/solicitud";
options.firstNameColumn = 4;		// Índice de la columna "nombre" en el archivo
options.lastNameColumn = 5;		// Índice de la columna "apellido" en el archivo
options.genderColumn = 6;		// Índice de la columna "género" en el archivo
options.ipAddressColumn = 7;		// Índice de la columna "dirección IP" en el archivo
options.hasHeaderRow = true;		// Si esto es `true`, la primera fila se considera encabezados de tabla
options.removeDuplicate = true;	// Si esto es `true`, los correos electrónicos duplicados se eliminarán

send_file(zb, file_path, email_address_column, options, on_success_send_file, on_error);
```

####### La API getfile permite a los usuarios obtener el archivo de resultados de validación para el archivo enviado utilizando la API sendfile
```c
void on_error(ZBErrorResponse error_response) {
    printf("%s\n\n", zb_error_response_to_string(&error_response));
}

void on_success_get_file(ZBGetFileResponse response) {
    printf("%s\n\n", zb_get_file_response_to_string(&response));
}

char* file_id = "<ID_DEL_ARCHIVO>";				// El ID de archivo devuelto al llamar a la API sendfile
char* local_download_path = "<RUTA_DESCARGA_ARCHIVO>"; 	// La ubicación donde se guardará el archivo descargado

get_file(zb, file_id, local_download_path, on_success_get_file, on_error);
```

####### Verifica el estado de un archivo cargado a través del método "sendFile"
```c
void on_error(ZBErrorResponse error_response) {
    printf("%s\n\n", zb_error_response_to_string(&error_response));
}

void on_success_file_status(ZBFileStatusResponse response) {
    printf("%s\n\n", zb_file_status_response_to_string(&response));
}

char* file_id = "<ID_DEL_ARCHIVO>";		// El ID de archivo devuelto al llamar a la API sendfile

file_status(zb, file_id, on_success_file_status, on_error);
```

####### Elimina el archivo que se envió mediante la API sendfile. El archivo solo se puede eliminar cuando su estado es _`Complete`_
```c
void on_error(ZBErrorResponse error_response) {
    printf("%s\n\n", zb_error_response_to_string(&error_response));
}

void on_success_delete_file(ZBDeleteFileResponse response) {
    printf("%s\n\n", zb_delete_file_response_to_string(&response));
}

char* file_id = "<ID_DEL_ARCHIVO>";		// El ID de archivo devuelto al llamar a la API sendfile

delete_file(zb, file_id, on_success_delete_file, on_error);
```

####### La API scoringSendfile permite al usuario enviar un archivo para validar correos electrónicos a granel


```c
void on_error(ZBErrorResponse error_response) {
    printf("%s\n\n", zb_error_response_to_string(&error_response));
}

void on_success_send_file(ZBSendFileResponse response) {
    printf("%s\n\n", zb_send_file_response_to_string(&response));
}

char* file_path = "<RUTA_DEL_ARCHIVO>";			// Ruta del archivo CSV o TXT
int email_address_column = 3; 			// Índice de la columna "email" en el archivo. El índice comienza en 1
SendFileOptions options = new_send_file_options();	// Opciones adicionales

options.returnUrl = "https://dominio.com/llamado/despues/procesamiento/solicitud";
options.hasHeaderRow = true;            // Si esto es `true`, la primera fila se considera encabezados de tabla

scoring_send_file(zb, file_path, email_address_column, options, on_success_send_file, on_error);
```

####### La API scoringGetFile permite a los usuarios obtener el archivo de resultados de validación para el archivo enviado utilizando la API scoringSendfile
```c
void on_error(ZBErrorResponse error_response) {
    printf("%s\n\n", zb_error_response_to_string(&error_response));
}

void on_success_get_file(ZBGetFileResponse response) {
    printf("%s\n\n", zb_get_file_response_to_string(&response));
}

char* file_id = "<ID_DEL_ARCHIVO>";                       	// El ID de archivo devuelto al llamar a la API scoringSendfile
char* local_download_path = "<RUTA_DESCARGA_ARCHIVO>";  	// La ubicación donde se guardará el archivo descargado

scoring_get_file(zb, file_id, local_download_path, on_success_get_file, on_error);
```

####### Verifica el estado de un archivo cargado a través del método "scoringSendFile"
```c
void on_error(ZBErrorResponse error_response) {
    printf("%s\n\n", zb_error_response_to_string(&error_response));
}

void on_success_file_status(ZBFileStatusResponse response) {
    printf("%s\n\n", zb_file_status_response_to_string(&response));
}

char* file_id = "<ID_DEL_ARCHIVO>";                    // El ID de archivo devuelto al llamar a la API scoringSendfile

scoring_file_status(zb, file_id, on_success_file_status, on_error);
```

####### Elimina el archivo que se envió mediante la API scoring scoringSendfile. El archivo solo se puede eliminar cuando su estado es _`Complete`_
```c
void on_error(ZBErrorResponse error_response) {
    printf("%s\n\n", zb_error_response_to_string(&error_response));
}

void on_success_delete_file(ZBDeleteFileResponse response) {
    printf("%s\n\n", zb_delete_file_response_to_string(&response));
}

char* file_id = "<ID_DEL_ARCHIVO>";                       // El ID de archivo devuelto al llamar a la API scoringSendfile

scoring_delete_file(zb, file_id, on_success_delete_file, on_error);
```

* ##### El buscador de correos electrónicos te permite identificar el formato correcto de correo electrónico cuando proporcionas un nombre y un dominio de correo electrónico o el nombre de una empresa.
```c
void error_callback(ZBErrorResponse error_response) {
    char* error_string = concatenate_strings(&(error_response.errors), "");
    printf("ERROR: %s\n", error_string);
    free(error_string);
}

void on_success_callback(ZBFindEmailResponse response) {
    char* result = zb_find_email_response_to_string(&response);
    printf("%s\n", result);
    free(result);
}

// actual method
find_email_by_domain(
    zb, "example.com", "John", "", "Doe", on_success_callback, error_callback
);

find_email_by_company_name(
    zb, "company", "John", "", "Doe", on_success_callback, error_callback
);
```

* ##### La búsqueda de dominio le permite identificar el dominio de correo electrónico cuando proporciona un dominio o nombre de empresa.
```c
void error_callback(ZBErrorResponse error_response) {
    char* error_string = concatenate_strings(&(error_response.errors), "");
    printf("ERROR: %s\n", error_string);
    free(error_string);
}

void on_success_callback(ZBDomainSearchResponse response) {
    char* result = zb_domain_search_response_to_string(&response);
    printf("%s\n", result);
    free(result);
}

// actual method
search_domain_by_domain(
    zb, "example.com", on_success_callback, error_callback
);

search_domain_by_company_name(
    zb, "company", on_success_callback, error_callback
);
```

#### DESARROLLO

Se recomienda realizar el desarrollo en Linux.

Después de verificar el repositorio, compila la biblioteca y luego ejecuta las pruebas.

```bash
./build/bin/ZeroBounceTests
```

Deberías ver una salida como esta:

```bash
24 pruebas 0 fallos 0 ignoradas
OK
```

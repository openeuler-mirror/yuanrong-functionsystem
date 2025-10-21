# 函数编程简单示例

本节通过一些示例介绍如何使用有状态函数、无状态函数开发应用，您可以基于这些示例作为基础工程快速上手。

:::{note}
运行示例需先[安装 openYuanrong](../../deploy/installation.md) SDK、命令行工具 yr，并已在[主机上](../../deploy/deploy_processes/index.md)部署 openYuanrong。
:::

(example-project-stateless-function)=

## 无状态函数示例工程

:::::{tab-set}

::::{tab-item} Python

1. 准备示例工程

    新建一个工作目录 `python-stateless-function`，包含文件结构如下。其中，example.py 为应用代码。

    ```bash
    python-stateless-function
    └── example.py
    ```

    :::{dropdown} example.py 文件内容
    :chevron: down-up
    :icon: chevron-down

    ```python
    import yr

    # Define stateless function
    @yr.invoke
    def say_hello(name):
        return 'hello, ' + name


    # Init only once
    yr.init()

    # Asynchronously invoke stateless functions in parallel
    results_ref = [say_hello.invoke('yuanrong') for i in range(3)]
    print(yr.get(results_ref))

    # Release environmental resources
    yr.finalize()
    ```

    :::

2. 运行程序

    在 `python-stateless-function` 目录下运行程序，输出如下：

    ```bash
    python example.py
    # ['hello, yuanrong', 'hello, yuanrong', 'hello, yuanrong']
    ```

::::

::::{tab-item} C++

:::{note}
示例运行依赖 make、g++ 及 cmake。
:::

1. 准备示例工程

    新建一个工作目录 `cpp-stateless-function`，包含文件结构如下。其中，build 目录用于存放编译构建中生成的文件，CMakeLists.txt 为 CMake 构建系统使用的配置文件，example.cpp 为应用代码。

    ```bash
    cpp-stateless-function
    ├── build
    ├── CMakeLists.txt
    └── src
        └── example.cpp
    ```

    :::{dropdown} example.cpp 文件内容
    :chevron: down-up
    :icon: chevron-down

    ```cpp
    #include <iostream>
    #include "yr/yr.h"

    // Define stateless function
    std::string SayHello(std::string name)
    {
        return "hello, " + name;
    }
    YR_INVOKE(SayHello)


    int main(int argc, char *argv[])
    {
        // Init only once
        YR::Init(YR::Config{}, argc, argv);

        // Asynchronously invoke stateless functions in parallel
        std::vector<YR::ObjectRef<std::string>> results_ref;
        for (int i = 0; i < 3; i++) {
            auto result_ref = YR::Function(SayHello).Invoke(std::string("yuanrong"));
            results_ref.emplace_back(result_ref);
        }

        for (auto result : YR::Get(results_ref)) {
            std::cout << *result << std::endl;
        }

        // Release environmental resources
        YR::Finalize();
        return 0;
    }
    ```

    :::
    :::{dropdown} CMakeLists.txt 文件内容，**须对应修改 YR_INSTALL_PATH 为您的 openYuanrong 安装路径**
    :chevron: down-up
    :icon: chevron-down

    ```cmake
    cmake_minimum_required(VERSION 3.16.1)
    # 指定项目名称，例如：cpp-stateless-function
    project(cpp-stateless-function LANGUAGES C CXX)
    set(CMAKE_CXX_STANDARD 17)

    # 指定编译生成文件路径在 build 目录下
    set(SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
    set(BINARY_DIR ${SOURCE_DIR}/build)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${BINARY_DIR})

    set(CMAKE_CXX_FLAGS "-pthread")
    set(BUILD_SHARED_LIBS ON)

    # 替换 YR_INSTALL_PATH 的值为 openYuanrong 安装路径，可通过 yr version 命令查看
    set(YR_INSTALL_PATH "/usr/local/lib/python3.9/site-packages/yr/inner")
    link_directories(${YR_INSTALL_PATH}/runtime/sdk/cpp/lib)
    include_directories(
        ${YR_INSTALL_PATH}/runtime/sdk/cpp/include
    )

    # 生成可执行文件 example，修改 example.cpp 为您对应的源码文件
    add_executable(example src/example.cpp)
    target_link_libraries(example yr-api)

    # 生成动态库文件 example-dll，修改 example.cpp 为您对应的源码文件
    add_library(example-dll SHARED src/example.cpp)
    target_link_libraries(example-dll yr-api)
    ```

    :::

2. 编译构建

    在 `cpp-stateless-function/build` 目录下，执行如下命令构建应用：

    ```bash
    cmake ..
    make
    ```

    成功构建将在该目录下生成 driver 程序 `example` 和包含无状态函数定义的动态库 `libexample-dll.so`。

    :::{note}
    无状态函数可能在 openYuanrong 集群的任意节点执行。因此，运行 driver 程序前，需要在其他节点相同路径下拷贝一份 `libexample-dll.so` 文件。
    :::

3. 运行程序

    在 `cpp-stateless-function/build` 目录下运行程序，输出如下：

    ```bash
    ./example --codePath=$(pwd)
    # hello, yuanrong
    # hello, yuanrong
    # hello, yuanrong
    ```

::::
::::{tab-item} Java

:::{note}
示例运行依赖 java 8 和 maven。
:::

1. 准备示例工程

    新建一个工作目录 `java-stateless-function`，包含文件结构如下。其中，pom.xml 为 maven 配置文件，Greeter.java 和 Main.java 为应用代码。

    ```bash
    java-stateless-function
    ├── pom.xml
    └── src
        └── main
            └── java
                └── com
                    └── yuanrong
                        └── example
                            ├── Greeter.java
                            └── Main.java
    ```

    :::{dropdown} pom.xml 文件内容，**请参考[安装 Java SDK](install-yuanrong-java-sdk) 并配置依赖 yr-api-sdk**
    :chevron: down-up
    :icon: chevron-down

    ```xml
    <?xml version="1.0" encoding="UTF-8"?>

    <project xmlns="http://maven.apache.org/POM/4.0.0"
            xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance"
            xsi:schemaLocation="http://maven.apache.org/POM/4.0.0 http://maven.apache.org/xsd/maven-4.0.0.xsd">
        <modelVersion>4.0.0</modelVersion>

        <groupId>com.yuanrong.example</groupId>
        <artifactId>example</artifactId>
        <version>1.0.0</version>

        <properties>
            <project.build.sourceEncoding>UTF-8</project.build.sourceEncoding>
            <maven.compiler.source>1.8</maven.compiler.source>
            <maven.compiler.target>1.8</maven.compiler.target>
        </properties>

        <dependencies>
            <dependency>
                <!-- 修改版本号为您实际使用版本 -->
                <groupId>com.yuanrong</groupId>
                <artifactId>yr-api-sdk</artifactId>
                <version>1.0.0</version>
            </dependency>
        </dependencies>

        <build>
            <plugins>
                <plugin>
                    <groupId>org.apache.maven.plugins</groupId>
                    <artifactId>maven-assembly-plugin</artifactId>
                    <executions>
                        <execution>
                            <phase>package</phase>
                            <goals>
                                <goal>single</goal>
                            </goals>
                        </execution>
                    </executions>
                    <configuration>
                        <archive>
                            <manifest>
                                <mainClass>com.yuanrong.example.Main</mainClass>
                            </manifest>
                        </archive>
                        <descriptorRefs>
                            <descriptorRef>jar-with-dependencies</descriptorRef>
                        </descriptorRefs>
                        <appendAssemblyId>false</appendAssemblyId>
                    </configuration>
                </plugin>
            </plugins>
        </build>
    </project>
    ```

    :::
    :::{dropdown} Greeter.java 文件内容
    :chevron: down-up
    :icon: chevron-down

    ```java
    package com.yuanrong.example;

    public class Greeter {
        // Define stateless function
        public static String sayHello(String name) {
            return "hello, " + name;
        }
    }
    ```

    :::
    :::
    :::{dropdown} Main.java 文件内容
    :chevron: down-up
    :icon: chevron-down

    ```java
    package com.yuanrong.example;

    import com.yuanrong.Config;
    import com.yuanrong.api.YR;
    import com.yuanrong.runtime.client.ObjectRef;
    import com.yuanrong.exception.YRException;

    import java.util.HashMap;
    import java.util.ArrayList;
    import java.util.List;

    public class Main {
        public static void main(String[] args) throws YRException {
            // Init only once
            YR.init(new Config());

            // Asynchronously invoke stateless functions in parallel
            List<ObjectRef> resultsRef = new ArrayList<>();
            for (int i = 0; i < 3; i++) {
                ObjectRef resultRef = YR.function(Greeter::sayHello).invoke("yuanrong");
                resultsRef.add(resultRef);
            }

            for (ObjectRef resultRef : resultsRef) {
                System.out.println(YR.get(resultRef, 30));
            }

            // Release environmental resources
            YR.Finalize();
        }
    }
    ```

    :::

2. 编译构建

    在 `java-stateless-function` 目录下，执行如下命令构建应用：

    ```bash
    mvn clean package
    ```

    成功构建将在 `java-stateless-function/target` 目录下生成 jar 包 `example-1.0.0.jar`。

    :::{note}
    无状态函数可能在 openYuanrong 集群的任意节点执行。因此，运行程序前，需要在其他节点相同路径下拷贝一份 `example-1.0.0.jar` 文件。
    :::

3. 运行程序

    在 `java-stateless-function` 目录下运行程序，输出如下：

    ```bash
    java -Dyr.codePath=$(pwd)/target -cp target/example-1.0.0.jar com.yuanrong.example.Main
    # hello, yuanrong
    # hello, yuanrong
    # hello, yuanrong
    ```

::::
:::::

(example-project-stateful-function)=

## 有状态函数示例工程

:::::{tab-set}
::::{tab-item} Python

1. 准备示例工程

    新建一个工作目录 `python-stateful-function`，包含文件结构如下。其中，example.py 为应用代码。

    ```bash
    python-stateful-function
    └── example.py
    ```

    :::{dropdown} example.py 文件内容
    :chevron: down-up
    :icon: chevron-down

    ```python
    # example.py
    import yr

    # Define stateful function
    @yr.instance
    class Object:
        def __init__(self):
            self.value = 0

        def save(self, value):
            self.value = value

        def get(self):
            return self.value


    # Init only once
    yr.init()

    # Create three stateful function instances
    objs = [Object.invoke() for i in range(3)]

    # Asynchronously invoke stateful functions in parallel
    [obj.save.invoke(9) for obj in objs]
    results_ref = [obj.get.invoke() for obj in objs]
    print(yr.get(results_ref))

    # Destroy stateful function instance
    [obj.terminate() for obj in objs]

    # Release environmental resources
    yr.finalize()
    ```

    :::

2. 运行程序

    在 `python-stateful-function` 目录下运行程序，输出如下：

    ```bash
    python example.py
    # [9, 9, 9]
    ```

::::
::::{tab-item} C++

:::{note}
示例运行依赖 make、g++ 及 cmake。
:::

1. 准备示例工程

    新建一个工作目录 `cpp-stateful-function`，包含文件结构如下。其中，build 目录用于存放编译构建中生成的文件，CMakeLists.txt 为 CMake 构建系统使用的配置文件，example.cpp 为应用代码。

    ```bash
    cpp-stateless-function
    ├── build
    ├── CMakeLists.txt
    └── src
        └── example.cpp
    ```

    :::{dropdown} example.cpp 文件内容
    :chevron: down-up
    :icon: chevron-down

    ```cpp
    #include <iostream>
    #include "yr/yr.h"

    // Define stateful function
    class Object {
    public:
        Object() {}
        Object(int value) { this->value = value; }

        static Object *FactoryCreate(int value) {
            return new Object(value);
        }

        void Save(int value) {
            this->value = value;
        }

        int Get() {
            return value;
        }

        YR_STATE(value);

    private:
        int value;
    };
    YR_INVOKE(Object::FactoryCreate, &Object::Save, &Object::Get);

    int main(int argc, char *argv[])
    {
        // Init only once
        YR::Init(YR::Config{}, argc, argv);

        // Create three stateful function instances
        std::vector<YR::NamedInstance<Object>> objects;
        for (int i = 0; i < 3; i++) {
            auto obj = YR::Instance(Object::FactoryCreate).Invoke(0);
            objects.emplace_back(obj);
        }

        // Asynchronously invoke stateful functions in parallel
        std::vector<YR::ObjectRef<int>> results_ref;
        for (auto obj : objects) {
            obj.Function(&Object::Save).Invoke(9);
            auto result_ref = obj.Function(&Object::Get).Invoke();
            results_ref.emplace_back(result_ref);
        }

        for (auto result : YR::Get(results_ref)) {
            std::cout << *result << std::endl;
        }

        // Destroy stateful function instance
        for (auto obj : objects) {
            obj.Terminate();
        }

        // Release environmental resources
        YR::Finalize();
        return 0;
    }
    ```

    :::
    :::{dropdown} CMakeLists.txt 文件内容，**须对应修改 YR_INSTALL_PATH 为您的 openYuanrong 安装路径**
    :chevron: down-up
    :icon: chevron-down

    ```cmake
    cmake_minimum_required(VERSION 3.16.1)
    # 指定项目名称，例如：cpp-stateful-function
    project(cpp-stateful-function LANGUAGES C CXX)
    set(CMAKE_CXX_STANDARD 17)

    # 指定编译生成文件路径在 build 目录下
    set(SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
    set(BINARY_DIR ${SOURCE_DIR}/build)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${BINARY_DIR})

    set(CMAKE_CXX_FLAGS "-pthread")
    set(BUILD_SHARED_LIBS ON)

    # 替换 YR_INSTALL_PATH 的值为 openYuanrong 安装路径，可通过 yr version 命令查看
    set(YR_INSTALL_PATH "/usr/local/lib/python3.9/site-packages/yr/inner")
    link_directories(${YR_INSTALL_PATH}/runtime/sdk/cpp/lib)
    include_directories(
        ${YR_INSTALL_PATH}/runtime/sdk/cpp/include
    )

    # 生成可执行文件 example，修改 example.cpp 为您对应的源码文件
    add_executable(example src/example.cpp)
    target_link_libraries(example yr-api)

    # 生成动态库文件 example-dll，修改 example.cpp 为您对应的源码文件
    add_library(example-dll SHARED src/example.cpp)
    target_link_libraries(example-dll yr-api)
    ```

    :::

2. 编译构建

    在 `cpp-stateful-function/build` 目录下，执行如下命令构建应用：

    ```bash
    cmake ..
    make
    ```

    成功构建将在该目录下生成 driver 程序 `example` 和包含有状态函数定义的动态库 `libexample-dll.so`。

    :::{note}
    有状态函数可能在 openYuanrong 集群的任意节点执行。因此，运行 driver 程序前，需要在其他节点相同路径下拷贝一份 `libexample-dll.so` 文件。
    :::

3. 运行程序

    在 `cpp-stateful-function/build` 目录下运行程序，输出如下：

    ```bash
    ./example --codePath=$(pwd)
    # 9
    # 9
    # 9
    ```

::::
::::{tab-item} Java

:::{note}
示例运行依赖 java 8 和 maven。
:::

1. 准备示例工程

    新建一个工作目录 `java-stateful-function`，包含文件结构如下。其中，pom.xml 为 maven 配置文件，Object.java 和 Main.java 为应用代码。

    ```bash
    java-stateless-function
    ├── pom.xml
    └── src
        └── main
            └── java
                └── com
                    └── yuanrong
                        └── example
                            ├── Object.java
                            └── Main.java
    ```

    :::{dropdown} pom.xml 文件内容，**请参考[安装 Java SDK](install-yuanrong-java-sdk) 并配置依赖 yr-api-sdk**
    :chevron: down-up
    :icon: chevron-down

    ```xml
    <?xml version="1.0" encoding="UTF-8"?>

    <project xmlns="http://maven.apache.org/POM/4.0.0"
            xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance"
            xsi:schemaLocation="http://maven.apache.org/POM/4.0.0 http://maven.apache.org/xsd/maven-4.0.0.xsd">
        <modelVersion>4.0.0</modelVersion>

        <groupId>com.yuanrong.example</groupId>
        <artifactId>example</artifactId>
        <version>1.0.0</version>

        <properties>
            <project.build.sourceEncoding>UTF-8</project.build.sourceEncoding>
            <maven.compiler.source>1.8</maven.compiler.source>
            <maven.compiler.target>1.8</maven.compiler.target>
        </properties>

        <dependencies>
            <dependency>
                <!-- 修改版本号为您实际使用版本 -->
                <groupId>com.yuanrong</groupId>
                <artifactId>yr-api-sdk</artifactId>
                <version>1.0.0</version>
            </dependency>
        </dependencies>

        <build>
            <plugins>
                <plugin>
                    <groupId>org.apache.maven.plugins</groupId>
                    <artifactId>maven-assembly-plugin</artifactId>
                    <executions>
                        <execution>
                            <phase>package</phase>
                            <goals>
                                <goal>single</goal>
                            </goals>
                        </execution>
                    </executions>
                    <configuration>
                        <archive>
                            <manifest>
                                <mainClass>com.yuanrong.example.Main</mainClass>
                            </manifest>
                        </archive>
                        <descriptorRefs>
                            <descriptorRef>jar-with-dependencies</descriptorRef>
                        </descriptorRefs>
                        <appendAssemblyId>false</appendAssemblyId>
                    </configuration>
                </plugin>
            </plugins>
        </build>
    </project>
    ```

    :::
    :::{dropdown} Object.java 文件内容
    :chevron: down-up
    :icon: chevron-down

    ```java
    package com.yuanrong.example;

    // Define stateful function
    public class Object {
        private int value = 0;

        public Object(int value) {
            this.value = value;
        }

        public void save(int value) {
            this.value = value;
        }

        public int get() {
            return this.value;
        }
    }
    ```

    :::
    :::
    :::{dropdown} Main.java 文件内容
    :chevron: down-up
    :icon: chevron-down

    ```java
    package com.yuanrong.example;

    import com.yuanrong.Config;
    import com.yuanrong.api.YR;
    import com.yuanrong.runtime.client.ObjectRef;
    import com.yuanrong.call.InstanceHandler;
    import com.yuanrong.exception.YRException;

    import java.util.HashMap;
    import java.util.ArrayList;
    import java.util.List;

    public class Main {
        public static void main(String[] args) throws YRException {
            // Init only once
            YR.init(new Config());

            // Create three stateful function instances
            List<InstanceHandler> objects = new ArrayList<>();
            for (int i = 0; i < 3; i++) {
                InstanceHandler obj = YR.instance(Object::new).invoke(0);
                objects.add(obj);
            }

            // Asynchronously invoke stateful functions in parallel
            List<ObjectRef> resultsRef = new ArrayList<>();
            for (InstanceHandler obj : objects) {
                obj.function(Object::save).invoke(9);
                ObjectRef resultRef = obj.function(Object::get).invoke();
                resultsRef.add(resultRef);
            }

            for (ObjectRef resultRef : resultsRef) {
                System.out.println(YR.get(resultRef, 30));
            }

            // Destroy stateful function instance
            for (InstanceHandler obj : objects) {
                obj.terminate();
            }

            // Release environmental resources
            YR.Finalize();
        }
    }
    ```

    :::

2. 编译构建

    在 `java-stateful-function` 目录下，执行如下命令构建应用：

    ```bash
    mvn clean package
    ```

    成功构建将在 `java-stateful-function/target` 目录下生成 jar 包 `example-1.0.0.jar`。

    :::{note}
    无状态函数可能在 openYuanrong 集群的任意节点执行。因此，运行程序前，需要在其他节点相同路径下拷贝一份 `example-1.0.0.jar` 文件。
    :::

3. 运行程序

    在 `java-stateful-function` 目录下运行程序，输出如下：

    ```bash
    java -Dyr.codePath=$(pwd)/target -cp target/example-1.0.0.jar com.yuanrong.example.Main
    # 9
    # 9
    # 9
    ```

::::
:::::

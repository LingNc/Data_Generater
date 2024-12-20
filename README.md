## 基于AI的ACM题目数据生成

### 1. 前置条件
要使用本项目，您需要拥有一个DeepSeek的API密钥(<a href="https://platform.deepseek.com/api_keys">点击申请</a>)。当第一次使用脚本时，该程序会要求输入apikey，之后该密钥会保存在本地，并与`ask.py`文件置于同一目录下。目前，本项目仅支持调用DeepSeek的API，因其价格最为经济。如需更换API服务，请在源码中进行相应修改。

### 2. 实现细节
本程序需要您提供题目的**题面描述**和**AC代码**。题面描述中必须明确包含题目编号，例如：A, B等。程序将根据这些信息，内置一个prompt，对题目和AC代码进行深入分析，以确定输入和输出格式。随后，通过调用DeepSeek的API接口，生成一个类似于`generate_A.py`的数据生成脚本。

`ask.py`程序首先会对AC代码进行编译，生成可执行文件。接着，它会调用`generate_A.py`脚本，利用AC代码运行并生成正确答案，并按照输入要求将数据存储在`./data_A`目录中。对于数据范围等细节，建议在数据生成后，根据需要在`generate_A.py`中进行手动调整。该文件内含详细的注释，方便您进行修改。

### 3. 注意事项
由于AI的局限性，对于复杂的题目（如涉及字符串处理或逻辑复杂的代码），直接生成可能会导致一些错误。建议您在生成后，先对`generate_A.py`中的代码进行检查和必要的修改。

此外，对于数据范围极大的题目，如果不提前进行调整，可能会生成过于庞大的数据文件（例如，几个GB的输入文件）。这是因为当前AI难以准确处理类似“**n的总和不超过10^5**”的描述。因此，请务必在生成数据前，对代码进行适当的修改，以避免生成不必要的大文件。
yr.UInt64Counter
==================

.. py:class:: yr.UInt64Counter(name: str, description: str, unit: str, labels: ~typing.Dict[str, str] = Field(name=None, type=None, default=<dataclasses._MISSING_TYPE object>, default_factory=<class 'dict'>, init=True, repr=True, hash=None, compare=True, metadata=mappingproxy({}), _field_type=None))

    基类：``Metrics``

    表示 64 位无符号整数计数器的类。

    参数：
        - **name** (str) - 计数器名称。
        - **description** (str) - 计数器描述。
        - **unit** (str，可选) - 单位。
        - **labels** (Dict[str, str]，可选) - 计数器的可选标签。

    **方法**：

    .. list-table::
       :header-rows: 0
       :widths: 30 70

       * - :ref:`__init__ <init_UInt64Counter>`
         -
       * - :ref:`add_labels <add_labels_UInt64Counter>`
         - 为 metrics 数据添加标签。
       * - :ref:`set <set_UInt64Counter>`
         - 将 uint64 计数器设置为给定的值。
       * - :ref:`reset <reset_UInt64Counter>`
         - 重置 uint64 计数器。
       * - :ref:`increase <increase_UInt64Counter>`
         - 将 uint64 计数器增加到给定的值。
       * - :ref:`get_value <get_value_UInt64Counter>`
         - 获取 uint64 计数器的值。



.. toctree::
    :maxdepth: 1
    :hidden:

    yr.UInt64Counter.__init__
    yr.UInt64Counter.add_labels
    yr.UInt64Counter.set
    yr.UInt64Counter.reset
    yr.UInt64Counter.increase
    yr.UInt64Counter.get_value

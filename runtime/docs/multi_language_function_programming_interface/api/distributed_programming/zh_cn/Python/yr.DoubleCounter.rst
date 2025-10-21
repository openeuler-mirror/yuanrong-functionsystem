yr.DoubleCounter
==================

.. py:class:: yr.DoubleCounter(name: str, description: str, unit: str, labels: ~typing.Dict[str, str] = Field(name=None, type=None, default=<dataclasses._MISSING_TYPE object>, default_factory=<class 'dict'>, init=True, repr=True, hash=None, compare=True, metadata=mappingproxy({}), _field_type=None))

    基类：``Metrics``

    表示双精度计数器类。

    参数：
        - **name** (str) - 计数器名称。
        - **description** (str) - 计数器描述。
        - **unit** (str，可选) - 单位。
        - **labels** (Dict[str, str]，可选) - 计数器的标签，默认为空字典。

    **方法**：

    .. list-table::
       :header-rows: 0
       :widths: 30 70

       * - :ref:`__init__ <init_DoubleCounter>`
         -
       * - :ref:`add_labels <add_labels_DoubleCounter>`
         - 为 metrics 数据添加标签。
       * - :ref:`set <set_DoubleCounter>`
         - 将双精度计数器设置为给定的值。
       * - :ref:`reset <reset_DoubleCounter>`
         - 重置双精度计数器。
       * - :ref:`increase <increase_DoubleCounter>`
         - 将双精度计数器增加到给定的值。
       * - :ref:`get_value <get_value_DoubleCounter>`
         - 获取双精度计数器的值。



.. toctree::
    :maxdepth: 1
    :hidden:

    yr.DoubleCounter.__init__
    yr.DoubleCounter.add_labels
    yr.DoubleCounter.set
    yr.DoubleCounter.reset
    yr.DoubleCounter.increase
    yr.DoubleCounter.get_value

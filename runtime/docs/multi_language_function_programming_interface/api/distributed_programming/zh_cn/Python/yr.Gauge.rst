yr.Gauge
=========

.. py:class:: yr.Gauge(name: str, description: str, unit: str = '', labels: Dict[str, str] = {})

    基类：``Metrics``

    用来上报 Metrics 数据。

    .. note::
        1. 类似 Prometheus 的数据结构。
        2. 计费信息会上报到后台的 openYuanrong 采集器。
        3. 不能在 driver 中使用。

    参数：
        - **name** (str) - 名称。
        - **description** (str) - 描述。
        - **unit** (str，可选) - 单位。
        - **labels** (Dict[str, str]，可选) - 标签。例如，``labels = {"request_id": "abc"}``。

    **方法**：

    .. list-table::
       :header-rows: 0
       :widths: 30 70

       * - :ref:`__init__ <init_Gauge>`
         -
       * - :ref:`add_labels <add_labels>`
         - 添加 label。
       * - :ref:`set <set_Gauge>`
         - 设置值。

.. toctree::
    :maxdepth: 1
    :hidden:

    yr.Gauge.__init__
    yr.Gauge.add_labels
    yr.Gauge.set

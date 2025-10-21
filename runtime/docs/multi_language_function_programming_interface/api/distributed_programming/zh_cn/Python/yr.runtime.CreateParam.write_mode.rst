.. _write_mode_CP:

yr.runtime.CreateParam.write_mode
----------------------------------------

.. py:attribute:: CreateParam.write_mode
   :type: WriteMode
   :value: 0

   配置数据可靠性设置。
   当服务器支持二级缓存（如Redis服务）以确保数据可靠性时，可通过该配置项启用保障机制。默认模式为 ``WriteMode.NONE_L2_CACHE`` （不启用二级缓存）。
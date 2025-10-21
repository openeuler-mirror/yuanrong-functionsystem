.. _resource_group_name_resource:

yr.ResourceGroup.resource_group_name
------------------------------------------
.. py:attribute:: ResourceGroup.resource_group_name
   :type: str

   返回当前资源组的名称。

   返回：
       当前资源组的名称。数据类型为str。

   样例：
       >>> rg = yr.create_resource_group([{"NPU":1},{"CPU":2000,"Memory":2000}], "rgname")
       >>> bundles = rg.bundle_specs



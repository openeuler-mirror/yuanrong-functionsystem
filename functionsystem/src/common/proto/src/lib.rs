pub mod common {
    tonic::include_proto!("common");
}

pub mod core_service {
    tonic::include_proto!("core_service");
}

pub mod runtime_service {
    tonic::include_proto!("runtime_service");
}

pub mod runtime_rpc {
    tonic::include_proto!("runtime_rpc");
}

pub mod bus_service {
    tonic::include_proto!("bus_service");
}

pub mod inner_service {
    tonic::include_proto!("inner_service");
}

pub mod exec_service {
    tonic::include_proto!("exec_service");
}

pub mod log_service {
    tonic::include_proto!("log_service");
}

pub mod runtime {
    pub mod v1 {
        tonic::include_proto!("runtime.v1");
    }
}

pub mod messages {
    tonic::include_proto!("messages");
}

pub mod resources {
    tonic::include_proto!("resources");
}

pub mod affinity {
    tonic::include_proto!("affinity");
}

pub mod bus_adapter {
    tonic::include_proto!("bus_adapter");
}

pub mod internal {
    pub mod generated {
        tonic::include_proto!("yr.internal");
    }

    pub use generated::*;
}

pub mod metastore {
    tonic::include_proto!("yr.internal.metastore");
}

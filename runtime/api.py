from fastapi import FastAPI, HTTPException
from pydantic import BaseModel
import subprocess
import logging

# Configure basic logging
logging.basicConfig(level=logging.INFO, format="%(asctime)s - %(levelname)s - %(message)s")

app = FastAPI(
    title="OT Container Management API",
    description="Use FastAPI to control the start, stop, and resource adjustments of Linux native containers.",
    version="1.0.0"
)

class ContainerAction(BaseModel):
    container_id: str

class ResourceUpdate(BaseModel):
    cpu_shares: int = None         # e.g., 512, 1024, etc.
    memory_limit: int = None       # in bytes; e.g., 134217728 for 128MB

@app.post("/api/containers/start", summary="Start Container")
def start_container(action: ContainerAction):
    """
    Start the specified container using runc.
    """
    logging.info(f"Attempting to start container: {action.container_id}")
    try:
        result = subprocess.run(
            ["sudo", "runc", "run", action.container_id],
            capture_output=True, text=True, check=True
        )
        logging.info(f"Container {action.container_id} started successfully.")
    except subprocess.CalledProcessError as e:
        logging.error(f"Failed to start container {action.container_id}: {e.stderr}")
        raise HTTPException(status_code=500, detail=f"Failed to start container: {e.stderr}")
    return {
        "message": f"Container {action.container_id} started successfully",
        "output": result.stdout
    }

@app.post("/api/containers/stop", summary="Stop Container")
def stop_container(action: ContainerAction):
    """
    Stop the specified container using runc by sending SIGKILL.
    """
    logging.info(f"Attempting to stop container: {action.container_id}")
    try:
        result = subprocess.run(
            ["sudo", "runc", "kill", action.container_id, "SIGKILL"],
            capture_output=True, text=True, check=True
        )
        logging.info(f"Container {action.container_id} stopped successfully.")
    except subprocess.CalledProcessError as e:
        logging.error(f"Failed to stop container {action.container_id}: {e.stderr}")
        raise HTTPException(status_code=500, detail=f"Failed to stop container: {e.stderr}")
    return {
        "message": f"Container {action.container_id} stopped successfully",
        "output": result.stdout
    }

@app.patch("/api/containers/{container_id}/resources", summary="Update Resource Settings")
def update_resources(container_id: str, update: ResourceUpdate):
    """
    Simulate updating the resource settings (e.g., CPU shares and memory limits)
    for the specified container.
    In a production environment, this would be done by modifying the corresponding
    configuration files in /sys/fs/cgroup.
    """
    logging.info(f"Updating resources for container: {container_id}")
    update_info = {}
    if update.cpu_shares is not None:
        update_info["cpu_shares"] = update.cpu_shares
        logging.info(f"Setting CPU shares to: {update.cpu_shares}")
    if update.memory_limit is not None:
        update_info["memory_limit"] = update.memory_limit
        logging.info(f"Setting memory limit to: {update.memory_limit} bytes")
    logging.info(f"Resource settings updated for container {container_id}: {update_info}")
    return {
        "message": f"Container {container_id} resource settings updated successfully",
        "updated": update_info
    }

@app.get("/api/containers", summary="Get All Containers")
def get_all_containers():
    """
    Retrieve a list of all containers using runc list.
    """
    logging.info("Fetching status for all containers.")
    try:
        result = subprocess.run(
            ["sudo", "runc", "list", "--format", "json"],
            capture_output=True, text=True, check=True
        )
        logging.info("All container statuses retrieved successfully.")
    except subprocess.CalledProcessError as e:
        logging.error(f"Failed to retrieve all container statuses: {e.stderr}")
        raise HTTPException(status_code=500, detail=f"Failed to retrieve container list: {e.stderr}")
    return {
        "message": "All containers retrieved successfully",
        "containers": result.stdout
    }

@app.get("/api/containers/{container_id}", summary="Get Container By ID")
def get_container_by_id(container_id: str):
    """
    Retrieve the state and details of the specified container using runc state.
    """
    logging.info(f"Fetching status for container: {container_id}")
    try:
        result = subprocess.run(
            ["sudo", "runc", "state", container_id],
            capture_output=True, text=True, check=True
        )
        logging.info(f"Container {container_id} status retrieved successfully.")
    except subprocess.CalledProcessError as e:
        logging.error(f"Failed to retrieve status for container {container_id}: {e.stderr}")
        raise HTTPException(status_code=500, detail=f"Failed to retrieve container state: {e.stderr}")
    return {
        "message": f"Container {container_id} status retrieved successfully",
        "state": result.stdout
    }


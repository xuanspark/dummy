"""Runtime settings loaded from environment / .env."""
from pydantic_settings import BaseSettings, SettingsConfigDict


class Settings(BaseSettings):
    model_config = SettingsConfigDict(env_prefix="ROBOT_", env_file=".env", extra="ignore")

    # Default values for the connect dialog. The backend never auto-connects;
    # the user picks a port via /api/ports and POSTs /api/connect.
    default_port: str = ""             # empty => "let the user pick"
    default_baud: int = 115200

    http_host: str = "0.0.0.0"
    http_port: int = 8000

    cmd_timeout: float = 5.0


settings = Settings()

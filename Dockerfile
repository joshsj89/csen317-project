FROM ubuntu:24.04

# Install system packages needed to run C++ binaries
RUN apt-get update && apt-get install -y \
    libstdc++6 \
    iputils-ping \
    netcat-openbsd \
    && apt-get clean

# Put our program inside the container
WORKDIR /app
COPY chord_sim /app/chord_sim
RUN chmod +x /app/chord_sim

# Expose ports (your nodes will pick their own ports)
EXPOSE 8000-9000

# This tells Docker what to run when the container starts
ENTRYPOINT ["/app/chord_sim"]

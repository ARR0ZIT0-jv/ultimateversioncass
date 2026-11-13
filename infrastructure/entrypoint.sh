#!/bin/bash
set -e

# Iniciar servidor SSH en segundo plano
/usr/sbin/sshd

ROLE=${NODE_ROLE:-worker}
HOSTNAME=$(hostname)

echo "=================================================="
echo " NODO DEL CLUSTER MPI INICIADO: $HOSTNAME ($ROLE)"
echo "=================================================="

if [ "$ROLE" = "worker" ]; then
    echo "[$HOSTNAME] Servicio SSH activo en puerto 22."
    echo "[$HOSTNAME] Listo para recibir tareas de cómputo del MASTER."
    exec tail -f /dev/null
elif [ "$ROLE" = "master" ]; then
    echo "[MASTER] Verificando conectividad SSH con worker1 y worker2..."
    
    until ssh -o ConnectTimeout=2 -q worker1 exit; do
        echo "[MASTER] Esperando a worker1..."
        sleep 1
    done
    echo "[MASTER] Conectividad con worker1: OK."

    until ssh -o ConnectTimeout=2 -q worker2 exit; do
        echo "[MASTER] Esperando a worker2..."
        sleep 1
    done
    echo "[MASTER] Conectividad con worker2: OK."

    # Compilar proyecto si es necesario
    if [ ! -f /workspace/epidemic_sim_mpi ]; then
        echo "[MASTER] Compilando motor distribuido epidemic_sim_mpi..."
        cd /workspace && make mpi
    fi

    echo "[MASTER] Clúster distribuido 100% operativo."
    echo "[MASTER] Para ejecutar simulación:"
    echo "         mpirun --allow-run-as-root --hostfile /workspace/infrastructure/hosts.ini -np 3 /workspace/epidemic_sim_mpi"

    if [ "$#" -gt 0 ]; then
        exec "$@"
    else
        exec tail -f /dev/null
    fi
fi

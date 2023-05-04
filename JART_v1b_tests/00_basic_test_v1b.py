# -*- coding: utf-8 -*-

# (C) Copyright 2020, 2021, 2022 IBM. All Rights Reserved.
#
# This code is licensed under the Apache License, Version 2.0. You may
# obtain a copy of this license in the LICENSE.txt file in the root directory
# of this source tree or at http://www.apache.org/licenses/LICENSE-2.0.
#
# Any modifications or derivative works of this code must retain this
# copyright notice, and modified files need to carry a notice indicating
# that they have been altered from the originals.

"""aihwkit example 1: simple network with one layer.

Simple network that consist of one analog layer. The network aims to learn
to sum all the elements from one array.
"""
# pylint: disable=invalid-name

# Imports from PyTorch.
import torch
from torch import Tensor
from torch.nn.functional import mse_loss

# Imports from aihwkit.
from aihwkit.nn import AnalogLinear
from aihwkit.optim import AnalogSGD
import JART_v1b_tests.yaml_loader as yaml_loader
from aihwkit.simulator.rpu_base import cuda

import argparse
parser = argparse.ArgumentParser()
parser.add_argument("-c", "--config", help="YAML Configuration File")
args = parser.parse_args()
if args.config:
    config_file = args.config
else:
    config_file = "noise_free.yml"

job_type, project_name, CUDA_Enabled, USE_wandb, USE_0_initialization, USE_bias, Repeat_Times, config_dictionary, JART_rpu_config = yaml_loader.from_yaml(config_file)

for repeat in range(Repeat_Times):
    if USE_wandb:
        import wandb
        wandb.init(project=project_name, group="Linear Regression", job_type=job_type)
        wandb.config.update(config_dictionary)

    # Prepare the datasets (input and expected output).
    slope = -0.5
    x = Tensor([[0.0], [1.0], [2.0], [3.0], [4.0]])
    y = Tensor([[0.0], [slope], [2*slope], [3*slope], [4*slope]])

    # Define a single-layer network.
    rpu_config = JART_rpu_config

    model = AnalogLinear(1, 1, bias=False,
                        rpu_config=rpu_config)
    model.set_weights(Tensor([[0.0]]))

    # Move the model and tensors to cuda if it is available.
    if cuda.is_compiled() & CUDA_Enabled:
        x = x.cuda()
        y = y.cuda()
        model.cuda()

    # Define an analog-aware optimizer, preparing it for using the layers.
    opt = AnalogSGD(model.parameters(), lr=config_dictionary["learning_rate"])
    opt.regroup_param_groups(model)

    weights = model.get_weights()[0][0][0]
    if USE_wandb:
        wandb.log({"Weight": weights, "epoch": 0})
    else:
        print('Epoch {} - Weight: {:.16f}'.format(
            0, weights))

    for epoch in range(config_dictionary["epochs"]):
        # Add the training Tensor to the model (input).
        pred = model(x)
        # Add the expected output Tensor.
        loss = mse_loss(pred, y)
        # Run training (backward propagation).
        loss.backward()

        opt.step()
        weights = model.get_weights()[0][0][0]
        if USE_wandb:
            wandb.log({"Weight": weights, "epoch": (epoch+1)})
        else:
            print('Epoch {} - Weight: {:.16f}'.format(
                (epoch+1), weights))
    
    if USE_wandb:
        wandb.finish()